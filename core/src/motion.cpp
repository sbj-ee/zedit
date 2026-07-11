#include "zedit/core/motion.hpp"

#include <algorithm>

namespace zedit::core {

namespace {

enum class CharClass { Space, Word, Punct, End };

char char_at(const PieceTable& buf, size_t offset) {
  if (offset >= buf.size()) {
    return '\0';
  }
  std::string s = buf.text_range(offset, 1);
  return s.empty() ? '\0' : s[0];
}

CharClass classify(char c) {
  if (c == '\0') {
    return CharClass::End;
  }
  if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
    return CharClass::Space;
  }
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') || c == '_') {
    return CharClass::Word;
  }
  return CharClass::Punct;
}

}  // namespace

size_t motion_word_forward(const PieceTable& buf, size_t offset) {
  size_t n = buf.size();
  if (offset >= n) {
    return n;
  }
  CharClass start_class = classify(char_at(buf, offset));
  size_t i = offset;
  if (start_class != CharClass::Space) {
    while (i < n && classify(char_at(buf, i)) == start_class) {
      ++i;
    }
  }
  while (i < n && classify(char_at(buf, i)) == CharClass::Space) {
    ++i;
  }
  return i;
}

size_t motion_word_backward(const PieceTable& buf, size_t offset) {
  if (offset == 0) {
    return 0;
  }
  size_t i = offset - 1;
  while (i > 0 && classify(char_at(buf, i)) == CharClass::Space) {
    --i;
  }
  if (classify(char_at(buf, i)) == CharClass::Space) {
    return 0;
  }
  CharClass cls = classify(char_at(buf, i));
  while (i > 0 && classify(char_at(buf, i - 1)) == cls) {
    --i;
  }
  return i;
}

size_t motion_word_end_forward(const PieceTable& buf, size_t offset) {
  size_t n = buf.size();
  if (n == 0) {
    return 0;
  }
  size_t i = offset + 1;
  while (i < n && classify(char_at(buf, i)) == CharClass::Space) {
    ++i;
  }
  if (i >= n) {
    return n - 1;
  }
  CharClass cls = classify(char_at(buf, i));
  while (i + 1 < n && classify(char_at(buf, i + 1)) == cls) {
    ++i;
  }
  return i;
}

size_t motion_line_start(const PieceTable& buf, size_t offset) {
  size_t line = buf.offset_to_line(offset);
  return buf.line_start_offset(line);
}

size_t motion_line_end(const PieceTable& buf, size_t offset) {
  size_t line = buf.offset_to_line(offset);
  size_t start = buf.line_start_offset(line);
  std::string text = buf.line_text(line);
  return text.empty() ? start : start + text.size() - 1;
}

Range text_object_inner_word(const PieceTable& buf, size_t offset) {
  size_t n = buf.size();
  if (n == 0) {
    return Range{0, 0};
  }
  offset = std::min(offset, n - 1);
  CharClass cls = classify(char_at(buf, offset));
  size_t start = offset;
  while (start > 0 && classify(char_at(buf, start - 1)) == cls) {
    --start;
  }
  size_t end = offset + 1;
  while (end < n && classify(char_at(buf, end)) == cls) {
    ++end;
  }
  return Range{start, end};
}

}  // namespace zedit::core
