#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace zedit::core {

enum class TokenKind {
  None,
  Comment,
  String,
  Number,
  Keyword,
  Type,
  Function,
  Variable,
  Operator,
  Punctuation,
  Constant,
};

struct HighlightSpan {
  size_t start_byte;
  size_t end_byte;
  TokenKind kind;
};

// Implementations reparse in set_text() (called whenever the buffer
// changes) and answer highlight() scoped to a byte range, so the renderer
// only ever pays for the visible portion of the document, not the whole
// file, no matter how large it is.
class Highlighter {
 public:
  virtual ~Highlighter() = default;
  virtual void set_text(std::string_view text) = 0;
  virtual std::vector<HighlightSpan> highlight(size_t start_byte, size_t end_byte) const = 0;
};

// The default highlighter for plain text and any file type without a
// grammar yet: never produces spans, so the renderer falls back to a
// single default color.
class PlainHighlighter : public Highlighter {
 public:
  void set_text(std::string_view) override {}
  std::vector<HighlightSpan> highlight(size_t, size_t) const override { return {}; }
};

}  // namespace zedit::core
