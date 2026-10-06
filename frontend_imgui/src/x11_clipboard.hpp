#pragma once

// Linux only (compiled when X11 headers are available). Plain C++
// interface: no Xlib types leak out, and libX11 is loaded with dlopen() at
// runtime -- exactly like GLFW 3.4 itself does -- so zedit gains no hard
// libX11 dependency and still starts on a Wayland-only system.

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "zedit/core/clipboard_source.hpp"

namespace zedit::frontend {

// Reads the X11 CLIPBOARD selection the way any X11 toolkit does: ask the
// owner for TARGETS, then XConvertSelection() each wanted target into a
// property on a private window and read it back, following the ICCCM INCR
// protocol for large transfers.
//
// Uses its own Display connection and an unmapped InputOnly window, so it
// never touches GLFW's event queue. Every wait is bounded (poll() on the
// connection with a deadline), so an owner that never answers costs at most
// `timeout_ms` instead of hanging the UI; that owner is then remembered
// and not asked again until the selection changes hands. (GLFW's own
// glfwGetClipboardString() waits forever, and only ever asks for
// UTF8_STRING/STRING.)
//
// Not thread-safe; use it from the UI thread.
class X11Clipboard final : public zedit::core::MimeClipboardSource {
 public:
  struct Options {
    int timeout_ms = 1000;          // per reply (and per INCR chunk)
    int total_timeout_ms = 5000;    // whole INCR transfer
    size_t max_bytes = 64u << 20;   // larger transfers are abandoned
  };

  // nullopt display_name means $DISPLAY. Returns nullptr when libX11 can't
  // be loaded or the display can't be opened.
  static std::unique_ptr<X11Clipboard> open(const char* display_name = nullptr);
  static std::unique_ptr<X11Clipboard> open(const char* display_name, Options options);
  ~X11Clipboard() override;
  X11Clipboard(const X11Clipboard&) = delete;
  X11Clipboard& operator=(const X11Clipboard&) = delete;

  // The CLIPBOARD owner's window XID, 0 when nobody owns it.
  unsigned long owner();

  std::optional<std::vector<std::string>> targets() override;
  Read read(const std::string& type) override;

  // Whether the last read() arrived in INCR chunks. For tests.
  bool last_read_used_incr() const;

  struct Impl;

 private:
  explicit X11Clipboard(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace zedit::frontend
