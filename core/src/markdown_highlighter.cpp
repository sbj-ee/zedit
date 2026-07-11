#include "zedit/core/markdown_highlighter.hpp"

#include <tree_sitter/api.h>

#include <cstring>
#include <functional>

#include "capture_kind.hpp"

namespace zedit::core {

namespace {

// Appends spans for every capture `query` matches within [start_byte,
// end_byte) of `tree`, shifting each span's byte positions by
// `region_offset` -- 0 for the block tree (already document-absolute),
// or an (inline) region's start_byte when called for the inline tree
// (whose own node positions are relative to that region's substring).
void collect_spans(std::vector<HighlightSpan>& spans, TSQuery* query, TSNode root,
                    size_t start_byte, size_t end_byte, size_t region_offset) {
  TSQueryCursor* cursor = ts_query_cursor_new();
  ts_query_cursor_set_byte_range(cursor, static_cast<uint32_t>(start_byte),
                                  static_cast<uint32_t>(end_byte));
  ts_query_cursor_exec(cursor, query, root);

  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor, &match)) {
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      const TSQueryCapture& capture = match.captures[i];
      uint32_t name_len = 0;
      const char* name = ts_query_capture_name_for_id(query, capture.index, &name_len);
      TokenKind kind = capture_name_to_kind(std::string_view(name, name_len));
      if (kind == TokenKind::None) {
        continue;
      }
      TSNode node = capture.node;
      spans.push_back(HighlightSpan{region_offset + ts_node_start_byte(node),
                                     region_offset + ts_node_end_byte(node), kind});
    }
  }

  ts_query_cursor_delete(cursor);
}

}  // namespace

MarkdownHighlighter::MarkdownHighlighter(const TSLanguage* block_language,
                                          const char* block_query_source,
                                          const TSLanguage* inline_language,
                                          const char* inline_query_source) {
  block_parser_ = ts_parser_new();
  ts_parser_set_language(block_parser_, block_language);
  uint32_t error_offset = 0;
  TSQueryError error_type = TSQueryErrorNone;
  block_query_ = ts_query_new(block_language, block_query_source,
                               static_cast<uint32_t>(std::strlen(block_query_source)),
                               &error_offset, &error_type);

  inline_parser_ = ts_parser_new();
  ts_parser_set_language(inline_parser_, inline_language);
  inline_query_ = ts_query_new(inline_language, inline_query_source,
                                static_cast<uint32_t>(std::strlen(inline_query_source)),
                                &error_offset, &error_type);
}

MarkdownHighlighter::~MarkdownHighlighter() {
  if (block_tree_) ts_tree_delete(block_tree_);
  if (block_query_) ts_query_delete(block_query_);
  if (block_parser_) ts_parser_delete(block_parser_);
  if (inline_query_) ts_query_delete(inline_query_);
  if (inline_parser_) ts_parser_delete(inline_parser_);
}

void MarkdownHighlighter::set_text(std::string_view text) {
  text_.assign(text);
  TSTree* new_tree = ts_parser_parse_string(block_parser_, nullptr, text_.data(),
                                             static_cast<uint32_t>(text_.size()));
  if (block_tree_) {
    ts_tree_delete(block_tree_);
  }
  block_tree_ = new_tree;
  find_inline_regions();
}

void MarkdownHighlighter::find_inline_regions() {
  inline_regions_.clear();
  if (!block_tree_) {
    return;
  }
  std::function<void(TSNode)> walk = [&](TSNode node) {
    if (std::strcmp(ts_node_type(node), "inline") == 0) {
      inline_regions_.emplace_back(ts_node_start_byte(node), ts_node_end_byte(node));
      return;  // (inline) nodes are leaves of the block tree, nothing nested to recurse into
    }
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
      walk(ts_node_child(node, i));
    }
  };
  walk(ts_tree_root_node(block_tree_));
}

std::vector<HighlightSpan> MarkdownHighlighter::highlight(size_t start_byte,
                                                            size_t end_byte) const {
  std::vector<HighlightSpan> spans;
  if (!block_tree_ || !block_query_) {
    return spans;
  }

  collect_spans(spans, block_query_, ts_tree_root_node(block_tree_), start_byte, end_byte, 0);

  for (const auto& [region_start, region_end] : inline_regions_) {
    if (region_end <= start_byte || region_start >= end_byte) {
      continue;  // outside the requested range -- skip the reparse entirely
    }
    std::string_view region_text(text_.data() + region_start, region_end - region_start);
    TSTree* inline_tree = ts_parser_parse_string(inline_parser_, nullptr, region_text.data(),
                                                  static_cast<uint32_t>(region_text.size()));
    collect_spans(spans, inline_query_, ts_tree_root_node(inline_tree), 0, region_text.size(),
                  region_start);
    ts_tree_delete(inline_tree);
  }

  return spans;
}

}  // namespace zedit::core
