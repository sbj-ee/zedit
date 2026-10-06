#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "zedit/core/clipboard_text.hpp"

namespace zedit::core {

// A desktop clipboard that is read one MIME type (or X11 target) at a time:
// first ask what it offers, then fetch individual types. This is how both
// X11 (TARGETS + XConvertSelection) and Wayland (wl_data_offer / wl-paste)
// work, unlike macOS where every flavor is just there to read.
//
// Implementations live in the frontend (frontend_imgui/src/x11_clipboard.cpp,
// wl_paste_clipboard.cpp); core only decides which types to ask for and in
// what order, so that logic can be tested with a fake source.
class MimeClipboardSource {
 public:
  enum class ReadStatus {
    ok,           // `data` holds the bytes the owner sent (may be empty)
    unavailable,  // the owner refused or doesn't offer this type
    timed_out,    // the owner didn't answer in time; stop asking it anything
  };
  struct Read {
    ReadStatus status = ReadStatus::unavailable;
    std::string data;
  };

  virtual ~MimeClipboardSource() = default;

  // Every type the clipboard currently offers (X11 target names such as
  // "UTF8_STRING" and MIME types such as "text/html" alike). nullopt means
  // nobody owns the clipboard, or the owner didn't answer in time. An empty
  // list means there is an owner that won't say what it offers (an X11
  // owner refusing TARGETS); plain-text types are then tried blindly.
  virtual std::optional<std::vector<std::string>> targets() = 0;

  // The clipboard's contents as `type`.
  virtual Read read(const std::string& type) = 0;
};

// Types tried for each flavor, in order. Comparison is ASCII
// case-insensitive (owners spell "charset=UTF-8" either way).
//   plain: UTF8_STRING, text/plain;charset=utf-8, text/plain, STRING
//   html:  text/html
//   rtf:   text/rtf, application/rtf, text/richtext
//   uri:   text/uri-list
const std::vector<std::string_view>& plain_text_mime_types();
const std::vector<std::string_view>& html_mime_types();
const std::vector<std::string_view>& rtf_mime_types();
const std::vector<std::string_view>& uri_list_mime_types();

// Reads the clipboard into ClipboardFlavors, lazily and in
// clipboard_plain_text()'s preference order: plain text first, then HTML,
// then RTF, then a URI list, stopping at the first flavor that yields text
// -- so an ordinary plain-text copy costs one TARGETS round trip plus one
// read, and the result is exactly what clipboard_plain_text() would pick
// from a fully read clipboard. Only types the clipboard says it offers are
// requested. Bytes are decoded to UTF-8 on the way in (UTF-16 with a BOM,
// as Firefox writes text/html on X11; STRING as Latin-1; stray trailing
// NULs dropped). Stops asking as soon as the owner times out.
ClipboardFlavors read_mime_clipboard(MimeClipboardSource& source);

// clipboard_plain_text(read_mime_clipboard(source)): what a paste inserts,
// or nullopt when nothing usable is on the clipboard (no owner, owner timed
// out, or no flavor converts to non-empty text).
std::optional<std::string> mime_clipboard_plain_text(MimeClipboardSource& source);

// Bytes of clipboard type `type` as UTF-8 text (see read_mime_clipboard()).
std::string decode_clipboard_text(std::string_view type, std::string_view bytes);

}  // namespace zedit::core
