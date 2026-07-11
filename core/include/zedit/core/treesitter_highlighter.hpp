#pragma once

#include <string>

#include "zedit/core/highlight.hpp"

struct TSLanguage;
struct TSParser;
struct TSTree;
struct TSQuery;

namespace zedit::core {

// Reparses the whole document on every set_text() call rather than doing a
// true incremental ts_tree_edit() -- simpler, and tree-sitter is fast
// enough that a full reparse per keystroke is imperceptible at editing
// sizes. highlight() is still only ever asked about the visible byte
// range, so rendering cost stays bounded by viewport size regardless.
class TreeSitterHighlighter : public Highlighter {
 public:
  // `query_source` must outlive this object (grammar query text is a
  // process-lifetime string constant baked in at build time).
  TreeSitterHighlighter(const TSLanguage* language, const char* query_source);
  ~TreeSitterHighlighter() override;
  TreeSitterHighlighter(const TreeSitterHighlighter&) = delete;
  TreeSitterHighlighter& operator=(const TreeSitterHighlighter&) = delete;

  void set_text(std::string_view text) override;
  std::vector<HighlightSpan> highlight(size_t start_byte, size_t end_byte) const override;

 private:
  TSParser* parser_ = nullptr;
  TSTree* tree_ = nullptr;
  TSQuery* query_ = nullptr;
  std::string text_;
};

}  // namespace zedit::core
