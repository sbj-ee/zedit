#include "linux_clipboard.hpp"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>

#include "wl_paste_clipboard.hpp"
#include "zedit/core/clipboard_source.hpp"
#include "zedit/core/clipboard_text.hpp"
#ifdef ZEDIT_HAVE_X11_CLIPBOARD
#include "x11_clipboard.hpp"
#endif

namespace zedit::frontend {

namespace {

bool env_set(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && *v != '\0';
}

// GLFW's own plain-text read. A clipboard without a plain-text flavor makes
// GLFW report GLFW_FORMAT_UNAVAILABLE through the error callback, which
// main.cpp prints; for a paste that's an expected outcome, not an error, so
// the callback is muted for the duration of the call.
std::optional<std::string> glfw_plain_text() {
  GLFWerrorfun previous = glfwSetErrorCallback(nullptr);
  const char* text = glfwGetClipboardString(nullptr);
  std::optional<std::string> result;
  if (text != nullptr) result = std::string(text);
  glfwSetErrorCallback(previous);
  zedit::core::ClipboardFlavors flavors;
  flavors.plain_text = std::move(result);
  return zedit::core::clipboard_plain_text(flavors);
}

#ifdef ZEDIT_HAVE_X11_CLIPBOARD
struct X11State {
  bool tried_open = false;
  std::unique_ptr<X11Clipboard> clipboard;
  unsigned long own_owner = 0;  // GLFW's helper window, once zedit has copied

  X11Clipboard* get() {
    if (!tried_open) {
      tried_open = true;
      clipboard = X11Clipboard::open();
    }
    return clipboard.get();
  }
};

X11State& x11_state() {
  static X11State state;
  return state;
}

std::optional<std::string> read_x11() {
  X11State& state = x11_state();
  X11Clipboard* x11 = state.get();
  if (x11 == nullptr) return glfw_plain_text();  // libX11 not loadable: as before
  unsigned long owner = x11->owner();
  if (owner == 0) return std::nullopt;
  if (owner == state.own_owner) return glfw_plain_text();
  return zedit::core::mime_clipboard_plain_text(*x11);
}
#endif

std::optional<std::string> read_wayland() {
  if (std::optional<std::string> plain = glfw_plain_text()) return plain;
  static WlPasteClipboard wl_paste;
  std::optional<std::string> text = zedit::core::mime_clipboard_plain_text(wl_paste);
  static bool hinted = false;
  if (wl_paste.program_missing() && !hinted) {
    hinted = true;
    std::fprintf(stderr,
                 "zedit: the clipboard has no plain text; install wl-clipboard (wl-paste) to "
                 "paste HTML/RTF-only copies on Wayland\n");
  }
  return text;
}

}  // namespace

LinuxClipboardBackend linux_clipboard_backend() {
#if defined(GLFW_PLATFORM_X11) && defined(GLFW_PLATFORM_WAYLAND)
  GLFWerrorfun previous = glfwSetErrorCallback(nullptr);
  int platform = glfwGetPlatform();  // 0 (and an error) before glfwInit()
  glfwSetErrorCallback(previous);
  if (platform == GLFW_PLATFORM_X11) return LinuxClipboardBackend::x11;
  if (platform == GLFW_PLATFORM_WAYLAND) return LinuxClipboardBackend::wayland;
  if (platform != 0) return LinuxClipboardBackend::other;
#endif
  if (env_set("WAYLAND_DISPLAY")) return LinuxClipboardBackend::wayland;
  const char* session = std::getenv("XDG_SESSION_TYPE");
  if (session != nullptr && std::string_view(session) == "wayland") {
    return LinuxClipboardBackend::wayland;
  }
  if (env_set("DISPLAY")) return LinuxClipboardBackend::x11;
  return LinuxClipboardBackend::other;
}

std::optional<std::string> read_linux_clipboard_text() {
  switch (linux_clipboard_backend()) {
    case LinuxClipboardBackend::x11:
#ifdef ZEDIT_HAVE_X11_CLIPBOARD
      return read_x11();
#else
      return glfw_plain_text();
#endif
    case LinuxClipboardBackend::wayland:
      return read_wayland();
    case LinuxClipboardBackend::other:
      break;
  }
  return glfw_plain_text();
}

void note_linux_clipboard_write() {
#ifdef ZEDIT_HAVE_X11_CLIPBOARD
  if (linux_clipboard_backend() != LinuxClipboardBackend::x11) return;
  X11State& state = x11_state();
  if (X11Clipboard* x11 = state.get()) state.own_owner = x11->owner();
#endif
}

}  // namespace zedit::frontend
