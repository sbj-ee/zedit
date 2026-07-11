#include "zedit/core/treesitter_highlighter.hpp"

#include <tree_sitter/api.h>

#include <cstring>

namespace zedit::core {

namespace {

// Capture names carry dotted suffixes ("function.special",
// "variable.builtin", ...); usually only the first segment decides the
// color. Markdown's "text.*" captures are the exception -- title/literal/
// uri/reference mean different things and need distinct colors, so they're
// matched on the full name before falling through to the generic rule.
TokenKind capture_name_to_kind(std::string_view name) {
  if (name == "text.title") return TokenKind::Keyword;
  if (name == "text.literal") return TokenKind::String;
  if (name == "text.uri") return TokenKind::String;
  if (name == "text.reference") return TokenKind::Constant;

  size_t dot = name.find('.');
  std::string_view base = (dot == std::string_view::npos) ? name : name.substr(0, dot);

  if (base == "comment") return TokenKind::Comment;
  if (base == "string") return TokenKind::String;
  if (base == "number") return TokenKind::Number;
  if (base == "keyword" || base == "label") return TokenKind::Keyword;
  if (base == "type" || base == "module") return TokenKind::Type;
  if (base == "function") return TokenKind::Function;
  if (base == "variable" || base == "property") return TokenKind::Variable;
  if (base == "operator") return TokenKind::Operator;
  if (base == "delimiter" || base == "punctuation") return TokenKind::Punctuation;
  if (base == "constant") return TokenKind::Constant;
  return TokenKind::None;
}

}  // namespace

TreeSitterHighlighter::TreeSitterHighlighter(const TSLanguage* language,
                                              const char* query_source) {
  parser_ = ts_parser_new();
  ts_parser_set_language(parser_, language);

  uint32_t error_offset = 0;
  TSQueryError error_type = TSQueryErrorNone;
  query_ = ts_query_new(language, query_source, static_cast<uint32_t>(std::strlen(query_source)),
                         &error_offset, &error_type);
}

TreeSitterHighlighter::~TreeSitterHighlighter() {
  if (tree_) ts_tree_delete(tree_);
  if (query_) ts_query_delete(query_);
  if (parser_) ts_parser_delete(parser_);
}

void TreeSitterHighlighter::set_text(std::string_view text) {
  text_.assign(text);
  TSTree* new_tree = ts_parser_parse_string(parser_, nullptr, text_.data(),
                                             static_cast<uint32_t>(text_.size()));
  if (tree_) {
    ts_tree_delete(tree_);
  }
  tree_ = new_tree;
}

std::vector<HighlightSpan> TreeSitterHighlighter::highlight(size_t start_byte,
                                                              size_t end_byte) const {
  std::vector<HighlightSpan> spans;
  if (tree_ == nullptr || query_ == nullptr) {
    return spans;
  }

  TSQueryCursor* cursor = ts_query_cursor_new();
  ts_query_cursor_set_byte_range(cursor, static_cast<uint32_t>(start_byte),
                                  static_cast<uint32_t>(end_byte));
  ts_query_cursor_exec(cursor, query_, ts_tree_root_node(tree_));

  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor, &match)) {
    for (uint16_t i = 0; i < match.capture_count; ++i) {
      const TSQueryCapture& capture = match.captures[i];
      uint32_t name_len = 0;
      const char* name = ts_query_capture_name_for_id(query_, capture.index, &name_len);
      TokenKind kind = capture_name_to_kind(std::string_view(name, name_len));
      if (kind == TokenKind::None) {
        continue;
      }
      TSNode node = capture.node;
      spans.push_back(HighlightSpan{ts_node_start_byte(node), ts_node_end_byte(node), kind});
    }
  }

  ts_query_cursor_delete(cursor);
  return spans;
}

}  // namespace zedit::core
