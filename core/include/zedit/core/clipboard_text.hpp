#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace zedit::core {

// Everything a desktop clipboard is offering that zedit knows how to turn
// into plain text, one field per flavor. The frontend fills in whichever
// flavors the platform clipboard actually has (macOS: public.utf8-plain-text,
// public.html, public.rtf, public.url/public.file-url); unset means "not on
// the clipboard". Core stays platform-free: this is just bytes.
//
// Why it exists: rich-text sources don't always offer a plain-text flavor.
// Some Electron/Chromium copy paths put only text/html on the clipboard, and
// some Mac apps put only RTF. GLFW's clipboard API only ever asks for plain
// text, so for those copies it returned nothing and paste silently did
// nothing (see clipboard_plain_text()).
struct ClipboardFlavors {
  std::optional<std::string> plain_text;  // text/plain, public.utf8-plain-text
  std::optional<std::string> html;        // text/html, public.html
  std::optional<std::string> rtf;         // text/rtf, application/rtf, public.rtf
  std::optional<std::string> uri_list;    // text/uri-list, public.url, public.file-url
};

// The text a paste should insert, or nullopt when the clipboard offers
// nothing usable. Always plain text -- rich formatting is never inserted.
// Preference order: plain text (when non-empty), then HTML converted to
// text, then RTF converted to text, then a URI list (one URI per line).
// Line endings are normalized to '\n' whichever flavor wins.
std::optional<std::string> clipboard_plain_text(const ClipboardFlavors& flavors);

// Same rule as clipboard_plain_text(): true exactly when it would return
// non-empty text. Used for Edit > Paste's enabled state so the menu and the
// paste key can never disagree.
bool clipboard_has_text(const ClipboardFlavors& flavors);

// Renders an HTML fragment (as found on a clipboard) to plain text, roughly
// like a browser's innerText: tags are dropped, whitespace collapses outside
// <pre>, block elements and <br> become line breaks, paragraphs and headings
// are separated by a blank line, table cells by tabs, entities are decoded,
// and <script>/<style>/<head>/comments are skipped. Lists keep their
// markers ("1. ", "2. " for <ol>, honoring start/value; "- " for <ul>) with
// nested items indented under their parent's text. Leading/trailing blank
// lines are trimmed.
std::string html_to_plain_text(std::string_view html);

// Minimal RTF-to-text stripper: \par, \line and backslash-newline become
// '\n', \tab and \cell become '\t', \'hh decodes as Windows-1252, \uN as
// Unicode (with its \ucN fallback characters skipped, surrogate pairs
// joined), escaped \\ \{ \} are literal, and non-text destinations
// (fonttbl, colortbl, stylesheet, info, pict, field instructions, anything
// marked \*) are skipped. Cocoa/Word list labels ({\listtext\t1.\t},
// {\pntext 1.\tab}) are kept as "1. ". Leading/trailing blank lines are
// trimmed.
std::string rtf_to_plain_text(std::string_view rtf);

// text/uri-list to text: one URI per line, '#' comment lines dropped.
std::string uri_list_to_plain_text(std::string_view uri_list);

}  // namespace zedit::core
