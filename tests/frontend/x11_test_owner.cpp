#include "x11_test_owner.hpp"

#include <X11/Xlib.h>

namespace zedit::test {

struct HungX11ClipboardOwner::Impl {
  Display* dpy = nullptr;
  Window window = 0;
  Atom clipboard = 0;
};

HungX11ClipboardOwner::HungX11ClipboardOwner() : impl_(std::make_unique<Impl>()) {
  impl_->dpy = XOpenDisplay(nullptr);
  if (impl_->dpy == nullptr) return;
  impl_->window =
      XCreateSimpleWindow(impl_->dpy, DefaultRootWindow(impl_->dpy), 0, 0, 1, 1, 0, 0, 0);
  impl_->clipboard = XInternAtom(impl_->dpy, "CLIPBOARD", False);
  XSetSelectionOwner(impl_->dpy, impl_->clipboard, impl_->window, CurrentTime);
  XSync(impl_->dpy, False);
}

HungX11ClipboardOwner::~HungX11ClipboardOwner() {
  if (impl_->dpy == nullptr) return;
  XDestroyWindow(impl_->dpy, impl_->window);
  XCloseDisplay(impl_->dpy);
}

bool HungX11ClipboardOwner::owns_clipboard() const {
  return impl_->dpy != nullptr &&
         XGetSelectionOwner(impl_->dpy, impl_->clipboard) == impl_->window;
}

}  // namespace zedit::test
