#pragma once

// Test helper (Linux): an X11 client that takes the CLIPBOARD selection and
// then never answers a single request -- a hung app. Its own translation
// unit because Xlib's macros (None, Bool, Status...) collide with zedit's
// headers.

#include <memory>

namespace zedit::test {

class HungX11ClipboardOwner {
 public:
  HungX11ClipboardOwner();  // takes CLIPBOARD on $DISPLAY
  ~HungX11ClipboardOwner();  // gives it up (destroys its window)
  HungX11ClipboardOwner(const HungX11ClipboardOwner&) = delete;
  HungX11ClipboardOwner& operator=(const HungX11ClipboardOwner&) = delete;

  // Whether it opened the display and owns CLIPBOARD.
  bool owns_clipboard() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace zedit::test
