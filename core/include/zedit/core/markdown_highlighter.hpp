#pragma once

#include <string>
#include <utility>
#include <vector>

#include "zedit/core/highlight.hpp"

struct TSLanguage;
struct TSParser;
struct TSTree;
struct TSQuery;

namespace zedit::core {

// tree-sitter-markdown ships its structure (headings, code fences, lists,
// block quotes) and inline formatting (emphasis, links, inline code,
// escapes) as two separate grammars, combined via tree-sitter's standard
// injection convention: the block grammar marks each span of raw
// paragraph/heading text as an (inline) node, and
// `(inline) @injection.content #set! injection.language "markdown_inline"`
// says "reparse that span with the inline grammar" -- the same mechanism
// nvim-treesitter and every other tree-sitter-aware markdown consumer
// uses. A plain TreeSitterHighlighter only ever runs one grammar, so
// block-only support (the original scope cut, still noted in
// FetchDeps.cmake's history) left emphasis/links/inline-code
// unhighlighted. This class runs both: the block grammar once over the
// whole document (same as TreeSitterHighlighter), plus the inline
// grammar re-run over just each (inline) region's text on demand, with
// its spans shifted back into document-absolute byte positions.
class MarkdownHighlighter : public Highlighter {
 public:
  // Both query sources must outlive this object (grammar query text is a
  // process-lifetime string constant baked in at build time).
  MarkdownHighlighter(const TSLanguage* block_language, const char* block_query_source,
                       const TSLanguage* inline_language, const char* inline_query_source);
  ~MarkdownHighlighter() override;
  MarkdownHighlighter(const MarkdownHighlighter&) = delete;
  MarkdownHighlighter& operator=(const MarkdownHighlighter&) = delete;

  void set_text(std::string_view text) override;
  std::vector<HighlightSpan> highlight(size_t start_byte, size_t end_byte) const override;

 private:
  TSParser* block_parser_ = nullptr;
  TSTree* block_tree_ = nullptr;
  TSQuery* block_query_ = nullptr;
  // Kept alive (not reconstructed) across set_text() calls -- only the
  // per-region source text is reparsed on demand, not the parser/query
  // themselves, since ts_query_new() re-compiling the query source on
  // every single keystroke for every paragraph in the document would be
  // needless overhead compared to reusing one parser/query pair.
  TSParser* inline_parser_ = nullptr;
  TSQuery* inline_query_ = nullptr;
  std::string text_;
  // Byte ranges of every (inline) node found in the block tree, computed
  // once in set_text() rather than walked again on every highlight() call.
  std::vector<std::pair<size_t, size_t>> inline_regions_;

  void find_inline_regions();
};

}  // namespace zedit::core
