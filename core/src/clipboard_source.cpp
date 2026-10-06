#include "zedit/core/clipboard_source.hpp"

#include <cstddef>
#include <cstdint>

namespace zedit::core {

namespace {

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}

void append_utf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

std::string latin1_to_utf8(std::string_view bytes) {
  std::string out;
  out.reserve(bytes.size());
  for (char c : bytes) append_utf8(out, static_cast<unsigned char>(c));
  return out;
}

std::string utf16_to_utf8(std::string_view bytes, bool little_endian) {
  std::string out;
  out.reserve(bytes.size());
  auto unit = [&](size_t i) -> uint32_t {
    auto lo = static_cast<unsigned char>(bytes[little_endian ? i : i + 1]);
    auto hi = static_cast<unsigned char>(bytes[little_endian ? i + 1 : i]);
    return (static_cast<uint32_t>(hi) << 8) | lo;
  };
  for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
    uint32_t u = unit(i);
    if (u >= 0xD800 && u <= 0xDBFF && i + 3 < bytes.size()) {
      uint32_t lo = unit(i + 2);
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        append_utf8(out, 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00));
        i += 2;
        continue;
      }
    }
    append_utf8(out, (u >= 0xD800 && u <= 0xDFFF) ? 0xFFFD : u);
  }
  return out;
}

bool valid_utf8(std::string_view s) {
  size_t i = 0;
  while (i < s.size()) {
    auto c = static_cast<unsigned char>(s[i]);
    size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
    if (len == 0 || i + len > s.size()) return false;
    for (size_t k = 1; k < len; ++k) {
      if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
    }
    i += len;
  }
  return true;
}

}  // namespace

const std::vector<std::string_view>& plain_text_mime_types() {
  static const std::vector<std::string_view> types = {
      "UTF8_STRING", "text/plain;charset=utf-8", "text/plain", "STRING"};
  return types;
}
const std::vector<std::string_view>& html_mime_types() {
  static const std::vector<std::string_view> types = {"text/html"};
  return types;
}
const std::vector<std::string_view>& rtf_mime_types() {
  static const std::vector<std::string_view> types = {"text/rtf", "application/rtf",
                                                      "text/richtext"};
  return types;
}
const std::vector<std::string_view>& uri_list_mime_types() {
  static const std::vector<std::string_view> types = {"text/uri-list"};
  return types;
}

std::string decode_clipboard_text(std::string_view type, std::string_view bytes) {
  auto strip_nuls = [](std::string text) {
    while (!text.empty() && text.back() == '\0') text.pop_back();
    return text;
  };
  if (bytes.size() >= 2) {
    auto b0 = static_cast<unsigned char>(bytes[0]);
    auto b1 = static_cast<unsigned char>(bytes[1]);
    // UTF-16 with a byte order mark: Firefox puts text/html on the X11
    // clipboard this way. Without a BOM, "x\0y\0" is unmistakably UTF-16LE.
    if (b0 == 0xFF && b1 == 0xFE) return strip_nuls(utf16_to_utf8(bytes.substr(2), true));
    if (b0 == 0xFE && b1 == 0xFF) return strip_nuls(utf16_to_utf8(bytes.substr(2), false));
    if (bytes.size() % 2 == 0 && b0 != 0 && b1 == 0) {
      return strip_nuls(utf16_to_utf8(bytes, true));
    }
  }
  // Some owners include the C string's terminating NUL.
  while (!bytes.empty() && bytes.back() == '\0') bytes.remove_suffix(1);
  if (bytes.size() >= 3 && bytes.substr(0, 3) == "\xEF\xBB\xBF") bytes.remove_prefix(3);
  // X11's STRING target is ISO-8859-1 by definition; anything else that
  // isn't valid UTF-8 is far more likely Latin-1 than garbage.
  if (iequals(type, "STRING") || !valid_utf8(bytes)) return latin1_to_utf8(bytes);
  return std::string(bytes);
}

ClipboardFlavors read_mime_clipboard(MimeClipboardSource& source) {
  ClipboardFlavors flavors;
  std::optional<std::vector<std::string>> offered = source.targets();
  if (!offered) return flavors;  // no owner, or it didn't answer
  const bool blind = offered->empty();
  bool timed_out = false;

  // The owner's own spelling of `type` (X11 atom names are
  // case-sensitive, so "charset=UTF-8" must be asked for as offered).
  auto offered_as = [&](std::string_view type) -> std::optional<std::string> {
    if (blind) return std::string(type);
    for (const std::string& t : *offered) {
      if (iequals(t, type)) return t;
    }
    return std::nullopt;
  };
  // The first offered type of a flavor that reads back non-empty; an empty
  // string if all that were offered read back empty; nullopt if none was
  // offered or readable.
  auto fetch = [&](const std::vector<std::string_view>& types,
                   bool try_blind) -> std::optional<std::string> {
    std::optional<std::string> result;
    for (std::string_view type : types) {
      if (blind && !try_blind) continue;
      std::optional<std::string> name = offered_as(type);
      if (!name) continue;
      MimeClipboardSource::Read r = source.read(*name);
      if (r.status == MimeClipboardSource::ReadStatus::timed_out) {
        timed_out = true;
        return std::nullopt;
      }
      if (r.status != MimeClipboardSource::ReadStatus::ok) continue;
      std::string text = decode_clipboard_text(type, r.data);
      if (!text.empty()) return text;
      result = std::string();
    }
    return result;
  };

  flavors.plain_text = fetch(plain_text_mime_types(), /*try_blind=*/true);
  if (timed_out || clipboard_has_text(flavors)) return flavors;
  flavors.html = fetch(html_mime_types(), false);
  if (timed_out || clipboard_has_text(flavors)) return flavors;
  flavors.rtf = fetch(rtf_mime_types(), false);
  if (timed_out || clipboard_has_text(flavors)) return flavors;
  flavors.uri_list = fetch(uri_list_mime_types(), false);
  return flavors;
}

std::optional<std::string> mime_clipboard_plain_text(MimeClipboardSource& source) {
  return clipboard_plain_text(read_mime_clipboard(source));
}

}  // namespace zedit::core
