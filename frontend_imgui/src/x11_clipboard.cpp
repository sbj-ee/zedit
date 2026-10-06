#include "x11_clipboard.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <poll.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

namespace zedit::frontend {

namespace {

using Clock = std::chrono::steady_clock;

// The handful of Xlib entry points this file uses, resolved from
// libX11.so.6 at runtime (GLFW does the same; see the header).
struct Xlib {
  void* handle = nullptr;
  decltype(&::XOpenDisplay) OpenDisplay = nullptr;
  decltype(&::XCloseDisplay) CloseDisplay = nullptr;
  decltype(&::XInternAtom) InternAtom = nullptr;
  decltype(&::XGetAtomNames) GetAtomNames = nullptr;
  decltype(&::XCreateWindow) CreateWindow = nullptr;
  decltype(&::XDestroyWindow) DestroyWindow = nullptr;
  decltype(&::XGetSelectionOwner) GetSelectionOwner = nullptr;
  decltype(&::XConvertSelection) ConvertSelection = nullptr;
  decltype(&::XCheckTypedWindowEvent) CheckTypedWindowEvent = nullptr;
  decltype(&::XGetWindowProperty) GetWindowProperty = nullptr;
  decltype(&::XDeleteProperty) DeleteProperty = nullptr;
  decltype(&::XFree) Free = nullptr;
  decltype(&::XFlush) Flush = nullptr;
  decltype(&::XPending) Pending = nullptr;
  decltype(&::XConnectionNumber) ConnectionNumberFn = nullptr;
  decltype(&::XSetErrorHandler) SetErrorHandler = nullptr;
  decltype(&::XSync) Sync = nullptr;

  template <typename F>
  bool resolve(F& fn, const char* name) {
    fn = reinterpret_cast<F>(dlsym(handle, name));
    return fn != nullptr;
  }

  bool load() {
    if (handle != nullptr) return true;
    handle = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (handle == nullptr) handle = dlopen("libX11.so", RTLD_LAZY | RTLD_LOCAL);
    if (handle == nullptr) return false;
    bool ok = resolve(OpenDisplay, "XOpenDisplay") && resolve(CloseDisplay, "XCloseDisplay") &&
              resolve(InternAtom, "XInternAtom") && resolve(GetAtomNames, "XGetAtomNames") &&
              resolve(CreateWindow, "XCreateWindow") &&
              resolve(DestroyWindow, "XDestroyWindow") &&
              resolve(GetSelectionOwner, "XGetSelectionOwner") &&
              resolve(ConvertSelection, "XConvertSelection") &&
              resolve(CheckTypedWindowEvent, "XCheckTypedWindowEvent") &&
              resolve(GetWindowProperty, "XGetWindowProperty") &&
              resolve(DeleteProperty, "XDeleteProperty") && resolve(Free, "XFree") &&
              resolve(Flush, "XFlush") && resolve(Pending, "XPending") &&
              resolve(ConnectionNumberFn, "XConnectionNumber") &&
              resolve(SetErrorHandler, "XSetErrorHandler") && resolve(Sync, "XSync");
    if (!ok) {
      dlclose(handle);
      handle = nullptr;
    }
    return ok;
  }
};

Xlib& xlib() {
  static Xlib lib;
  return lib;
}

int ignore_x_error(Display*, XErrorEvent*) { return 0; }

// Xlib's default error handler exits the process. A clipboard owner can
// hand back anything (e.g. bogus atoms in its TARGETS reply), so X errors
// raised while talking to it are ignored instead -- installed only for the
// duration of one read and then restored, the way GLFW does it.
class ErrorTrap {
 public:
  ErrorTrap(Xlib& x, Display* dpy) : x_(x), dpy_(dpy), previous_(x.SetErrorHandler(ignore_x_error)) {}
  ~ErrorTrap() {
    x_.Sync(dpy_, False);
    x_.SetErrorHandler(previous_);
  }
  ErrorTrap(const ErrorTrap&) = delete;
  ErrorTrap& operator=(const ErrorTrap&) = delete;

 private:
  Xlib& x_;
  Display* dpy_;
  XErrorHandler previous_;
};

}  // namespace

struct X11Clipboard::Impl {
  Xlib& x = xlib();
  Options options;
  Display* dpy = nullptr;
  Window window = None;
  Atom clipboard = None;
  Atom targets_atom = None;
  Atom incr = None;
  Atom property = None;
  // An owner that didn't answer in time; not asked again while it still
  // owns the clipboard, so a hung app costs one timeout, not one per paste.
  Window hung_owner = None;
  bool last_incr = false;
  // Which text flavor each type atom we know belongs to, to catch owners
  // that answer any request with whatever they hold (xclip replies to a
  // UTF8_STRING request with its text/html data, typed text/html).
  std::vector<std::pair<Atom, int>> flavor_of_atom;

  int flavor(Atom a) const {
    for (const auto& [atom, f] : flavor_of_atom) {
      if (atom == a) return f;
    }
    return -1;
  }
  // A reply typed as a different known flavor than the one asked for (HTML
  // for a plain-text request, say) is not what we asked for. GLFW rejects
  // any type mismatch; types we don't know (ATOM for TARGETS,
  // COMPOUND_TEXT...) are let through.
  bool type_mismatch(Atom target, Atom type) const {
    int want = flavor(target);
    int got = flavor(type);
    return want >= 0 && got >= 0 && want != got;
  }

  ~Impl() {
    if (dpy == nullptr) return;
    if (window != None) x.DestroyWindow(dpy, window);
    x.CloseDisplay(dpy);
  }

  // Waits for an event of `type` on our window until `deadline`.
  bool wait_for(int type, XEvent& ev, Clock::time_point deadline) {
    const int fd = x.ConnectionNumberFn(dpy);
    for (;;) {
      if (x.CheckTypedWindowEvent(dpy, window, type, &ev)) return true;
      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
      if (remaining.count() <= 0) return false;
      pollfd pfd{fd, POLLIN, 0};
      int rc = poll(&pfd, 1, static_cast<int>(remaining.count()));
      if (rc < 0 && errno != EINTR) return false;
      x.Pending(dpy);  // reads whatever arrived into the event queue
    }
  }

  void discard(int type) {
    XEvent ev;
    while (x.CheckTypedWindowEvent(dpy, window, type, &ev)) {
    }
  }

  struct Property {
    bool exists = false;
    Atom type = None;
    std::string bytes;
  };

  // Reads (and deletes) our transfer property.
  Property take_property() {
    Property p;
    Atom type = None;
    int format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    if (x.GetWindowProperty(dpy, window, property, 0, LONG_MAX / 4, True, AnyPropertyType, &type,
                            &format, &nitems, &bytes_after, &data) != Success) {
      return p;
    }
    p.exists = type != None;
    p.type = type;
    if (data != nullptr) {
      // Format-32 data comes back as an array of C longs (8 bytes on LP64).
      size_t unit = format == 32 ? sizeof(long) : format == 16 ? sizeof(short) : 1;
      p.bytes.assign(reinterpret_cast<const char*>(data), nitems * unit);
      x.Free(data);
    }
    return p;
  }

  // XConvertSelection(CLIPBOARD -> target) and read the reply, INCR included.
  Read convert(Atom target) {
    Read result;
    last_incr = false;
    discard(SelectionNotify);
    x.DeleteProperty(dpy, window, property);
    x.ConvertSelection(dpy, clipboard, target, property, window, CurrentTime);
    x.Flush(dpy);

    const auto reply_deadline = Clock::now() + std::chrono::milliseconds(options.timeout_ms);
    XEvent ev;
    for (;;) {
      if (!wait_for(SelectionNotify, ev, reply_deadline)) {
        result.status = ReadStatus::timed_out;
        return result;
      }
      if (ev.xselection.selection == clipboard && ev.xselection.target == target) break;
    }
    if (ev.xselection.property == None) return result;  // refused: unavailable

    Property first = take_property();
    if (!first.exists) return result;
    if (first.type != incr) {
      if (type_mismatch(target, first.type)) return result;
      result.status = ReadStatus::ok;
      result.data = std::move(first.bytes);
      return result;
    }

    // INCR: deleting the property (done by take_property) tells the owner
    // to start; each chunk arrives as a new value of the property, and a
    // zero-length value ends the transfer. Events queued before now (the
    // INCR announcement itself) are stale.
    discard(PropertyNotify);
    last_incr = true;
    const auto total_deadline =
        Clock::now() + std::chrono::milliseconds(options.total_timeout_ms);
    std::string data;
    for (;;) {
      auto chunk_deadline = std::min(total_deadline,
                                     Clock::now() + std::chrono::milliseconds(options.timeout_ms));
      if (!wait_for(PropertyNotify, ev, chunk_deadline)) {
        result.status = ReadStatus::timed_out;
        return result;
      }
      if (ev.xproperty.atom != property || ev.xproperty.state != PropertyNewValue) continue;
      Property chunk = take_property();
      if (!chunk.exists) continue;  // already consumed with an earlier event
      if (chunk.bytes.empty()) break;
      if (data.empty() && type_mismatch(target, chunk.type)) return result;
      data += chunk.bytes;
      if (data.size() > options.max_bytes) return result;  // too big: unavailable
    }
    result.status = ReadStatus::ok;
    result.data = std::move(data);
    return result;
  }
};

std::unique_ptr<X11Clipboard> X11Clipboard::open(const char* display_name) {
  return open(display_name, Options{});
}

std::unique_ptr<X11Clipboard> X11Clipboard::open(const char* display_name, Options options) {
  auto impl = std::make_unique<Impl>();
  impl->options = options;
  Xlib& x = impl->x;
  if (!x.load()) return nullptr;
  impl->dpy = x.OpenDisplay(display_name);
  if (impl->dpy == nullptr) return nullptr;
  XSetWindowAttributes attrs{};
  attrs.event_mask = PropertyChangeMask;
  impl->window = x.CreateWindow(impl->dpy, DefaultRootWindow(impl->dpy), 0, 0, 1, 1, 0,
                                CopyFromParent, InputOnly, nullptr, CWEventMask, &attrs);
  impl->clipboard = x.InternAtom(impl->dpy, "CLIPBOARD", False);
  impl->targets_atom = x.InternAtom(impl->dpy, "TARGETS", False);
  impl->incr = x.InternAtom(impl->dpy, "INCR", False);
  impl->property = x.InternAtom(impl->dpy, "ZEDIT_CLIPBOARD_TRANSFER", False);
  int family = 0;
  for (const auto* types : {&zedit::core::plain_text_mime_types(), &zedit::core::html_mime_types(),
                            &zedit::core::rtf_mime_types(), &zedit::core::uri_list_mime_types()}) {
    for (std::string_view type : *types) {
      impl->flavor_of_atom.emplace_back(x.InternAtom(impl->dpy, std::string(type).c_str(), False),
                                        family);
    }
    ++family;
  }
  x.Flush(impl->dpy);
  return std::unique_ptr<X11Clipboard>(new X11Clipboard(std::move(impl)));
}

X11Clipboard::X11Clipboard(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
X11Clipboard::~X11Clipboard() = default;

unsigned long X11Clipboard::owner() {
  return impl_->x.GetSelectionOwner(impl_->dpy, impl_->clipboard);
}

std::optional<std::vector<std::string>> X11Clipboard::targets() {
  Impl& d = *impl_;
  ErrorTrap trap(d.x, d.dpy);
  Window current = owner();
  if (current == None) return std::nullopt;
  if (current == d.hung_owner) return std::nullopt;
  d.hung_owner = None;

  Read r = d.convert(d.targets_atom);
  if (r.status == ReadStatus::timed_out) {
    d.hung_owner = current;
    return std::nullopt;
  }
  std::vector<std::string> names;
  if (r.status != ReadStatus::ok) return names;  // owner won't list targets

  std::vector<Atom> atoms(r.data.size() / sizeof(Atom));
  std::memcpy(atoms.data(), r.data.data(), atoms.size() * sizeof(Atom));
  if (atoms.empty()) return names;
  std::vector<char*> raw(atoms.size(), nullptr);
  if (d.x.GetAtomNames(d.dpy, atoms.data(), static_cast<int>(atoms.size()), raw.data())) {
    for (char* name : raw) {
      if (name == nullptr) continue;
      names.emplace_back(name);
      d.x.Free(name);
    }
  }
  return names;
}

bool X11Clipboard::last_read_used_incr() const { return impl_->last_incr; }

X11Clipboard::Read X11Clipboard::read(const std::string& type) {
  Impl& d = *impl_;
  ErrorTrap trap(d.x, d.dpy);
  Atom target = d.x.InternAtom(d.dpy, type.c_str(), False);
  Read r = d.convert(target);
  if (r.status == ReadStatus::timed_out) d.hung_owner = owner();
  return r;
}

}  // namespace zedit::frontend
