#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Small UTF-8 helpers. Buffer columns stay byte offsets; these keep cursor
// movement and deletion on code point boundaries and convert to the UTF-16
// columns LSP uses. Invalid bytes count as one code point each, so nothing
// here can loop or skip valid text when a file isn't valid UTF-8.
namespace zedit::core {

inline bool utf8_is_continuation(unsigned char b) { return (b & 0xC0) == 0x80; }

// Byte length of the sequence a lead byte starts (1 for an invalid lead).
inline size_t utf8_sequence_length(unsigned char lead) {
  if (lead < 0x80) return 1;
  if ((lead & 0xE0) == 0xC0 && lead >= 0xC2) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0 && lead <= 0xF4) return 4;
  return 1;
}

// Length of the (possibly invalid) code point starting at `i`: its declared
// length if every continuation byte is present, else 1.
inline size_t utf8_char_length_at(std::string_view s, size_t i) {
  if (i >= s.size()) return 0;
  size_t n = utf8_sequence_length(static_cast<unsigned char>(s[i]));
  if (i + n > s.size()) return 1;
  for (size_t k = 1; k < n; ++k) {
    if (!utf8_is_continuation(static_cast<unsigned char>(s[i + k]))) return 1;
  }
  return n;
}

// Start of the code point after the one at `i` (clamped to size()).
inline size_t utf8_next(std::string_view s, size_t i) {
  if (i >= s.size()) return s.size();
  return i + utf8_char_length_at(s, i);
}

// Start of the code point containing byte `i` (i itself if it starts one).
inline size_t utf8_floor(std::string_view s, size_t i) {
  if (i >= s.size()) return s.size();
  for (size_t back = 1; back <= 3 && back <= i; ++back) {
    if (!utf8_is_continuation(static_cast<unsigned char>(s[i - back + 1]))) break;
    size_t start = i - back;
    if (!utf8_is_continuation(static_cast<unsigned char>(s[start]))) {
      return utf8_char_length_at(s, start) > back ? start : i;
    }
  }
  return i;
}

// Start of the code point before byte `i`.
inline size_t utf8_prev(std::string_view s, size_t i) {
  if (i == 0) return 0;
  if (i > s.size()) i = s.size();
  return utf8_floor(s, i - 1);
}

// UTF-8 for one code point; surrogates and out-of-range values become U+FFFD.
inline std::string utf8_encode(uint32_t cp) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
  std::string out;
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
  return out;
}

// LSP positions count UTF-16 code units. Byte column -> UTF-16 column.
inline size_t utf8_col_to_utf16(std::string_view line, size_t byte_col) {
  size_t units = 0;
  size_t i = 0;
  while (i < line.size() && i < byte_col) {
    size_t n = utf8_char_length_at(line, i);
    units += (n == 4) ? 2 : 1;
    i += n;
  }
  return units;
}

// UTF-16 column -> byte column (clamped to the line; a column inside a
// surrogate pair lands on the start of that code point).
inline size_t utf16_col_to_utf8(std::string_view line, size_t utf16_col) {
  size_t units = 0;
  size_t i = 0;
  while (i < line.size()) {
    size_t n = utf8_char_length_at(line, i);
    size_t w = (n == 4) ? 2 : 1;
    if (units + w > utf16_col) break;
    units += w;
    i += n;
  }
  return i;
}

}  // namespace zedit::core
