#pragma once

// macOS-only (compiled from macos_pasteboard.mm when APPLE). Plain C++
// interface so app.cpp and the tests never see Objective-C types.

#include <optional>
#include <string>

#include "zedit/core/clipboard_text.hpp"

namespace zedit::frontend {

// Reads every flavor zedit can turn into text from a pasteboard:
// public.utf8-plain-text, public.html, public.rtf (or, failing that, RTFD
// flattened to its string), and public.url / public.file-url. An empty
// `pasteboard_name` means the general pasteboard -- the one Cmd-C/Cmd-V use.
zedit::core::ClipboardFlavors read_macos_pasteboard(const std::string& pasteboard_name = {});

// What a paste should insert from the general pasteboard:
// core::clipboard_plain_text() over read_macos_pasteboard(). Cached on the
// pasteboard's changeCount, so calling it every frame (Edit menu's enabled
// state) costs nothing until something is copied.
//
// This replaces glfwGetClipboardString() for reads on macOS: GLFW only
// accepts NSPasteboardTypeString and returns NULL for an HTML-only or
// RTF-only copy, which made paste silently do nothing.
std::optional<std::string> read_macos_clipboard_text();

// Clears a pasteboard and puts exactly the given flavors on it (plain text,
// HTML and RTF; uri_list is ignored). For tests: lets them build an
// HTML-only or RTF-only pasteboard like the ones that broke paste.
void write_macos_pasteboard(const std::string& pasteboard_name,
                            const zedit::core::ClipboardFlavors& flavors);

// Whether a pasteboard currently offers the given UTI (e.g.
// "public.utf8-plain-text"). For tests.
bool macos_pasteboard_has_type(const std::string& pasteboard_name, const std::string& uti);

// A private, uniquely named pasteboard (so tests don't clobber the user's
// clipboard), and its release.
std::string create_unique_macos_pasteboard();
void release_macos_pasteboard(const std::string& pasteboard_name);

}  // namespace zedit::frontend
