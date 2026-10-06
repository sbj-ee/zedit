#pragma once

// Linux only (X11 and Wayland). Plain C++ interface for app.cpp and the
// tests; GLFW must be initialized before calling these.

#include <optional>
#include <string>

namespace zedit::frontend {

enum class LinuxClipboardBackend { x11, wayland, other };

// Which windowing system GLFW is running on: glfwGetPlatform() (GLFW 3.4+),
// or, before glfwInit()/on older GLFW, $WAYLAND_DISPLAY / $XDG_SESSION_TYPE
// / $DISPLAY.
LinuxClipboardBackend linux_clipboard_backend();

// What a paste should insert: the Linux counterpart of
// read_macos_clipboard_text(), and the clipboard bridge's read callback on
// Linux. Always plain text, chosen by core::clipboard_plain_text()'s rule
// (text/plain, then text/html -> text, then RTF -> text, then
// text/uri-list); nullopt when nothing usable is on the clipboard (paste
// then falls back to zedit's own register, as before).
//
// GLFW's glfwGetClipboardString() only ever asks for plain text
// (UTF8_STRING/STRING on X11, text/plain;charset=utf-8 on Wayland), so a
// copy offering only text/html (some Electron/Chromium paths) or only RTF
// pasted nothing. Instead:
//  - X11: reads CLIPBOARD itself (x11_clipboard.hpp): TARGETS, then each
//    wanted target via XConvertSelection, INCR included, every wait
//    bounded by a timeout. When zedit itself owns the clipboard (it copied
//    last), GLFW answers from its own copy as before.
//  - Wayland: GLFW's plain-text read first (that also covers zedit's own
//    copies); only if that yields nothing, `wl-paste` (wl_paste_clipboard.hpp)
//    is asked for the other types, with a timeout. Without wl-clipboard
//    installed this degrades to plain-text-only, as before.
std::optional<std::string> read_linux_clipboard_text();

// Call right after zedit puts text on the clipboard (via GLFW). On X11 this
// records which window now owns CLIPBOARD -- GLFW's private helper window
// -- so read_linux_clipboard_text() can recognise zedit's own copies and
// let GLFW answer them: zedit's UI thread is blocked during the read, so
// asking our own GLFW window over the wire could only time out. Copy-out
// itself is unchanged.
void note_linux_clipboard_write();

}  // namespace zedit::frontend
