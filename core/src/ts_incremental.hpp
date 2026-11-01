#pragma once

#include <tree_sitter/api.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

namespace zedit::core::detail {

inline TSPoint ts_point_at(std::string_view text, size_t byte) {
  TSPoint p{0, 0};
  size_t line_start = 0;
  for (size_t i = 0; i < byte; ++i) {
    if (text[i] == '\n') {
      ++p.row;
      line_start = i + 1;
    }
  }
  p.column = static_cast<uint32_t>(byte - line_start);
  return p;
}

// Replaces `text` with `new_text` and reparses. When there is an old tree,
// the changed span (common prefix/suffix trimmed) is described to
// tree-sitter with ts_tree_edit() and the old tree is passed to the parser,
// so only the region around the edit is re-lexed instead of the whole file.
// Takes ownership of `old_tree` and returns the new tree. `incremental` is
// set to whether the old tree was reused.
inline TSTree* reparse(TSParser* parser, TSTree* old_tree, std::string& text,
                       std::string_view new_text, bool& incremental) {
  incremental = false;
  if (old_tree) {
    if (text == new_text) {
      incremental = true;
      return old_tree;  // nothing changed
    }
    size_t max_common = std::min(text.size(), new_text.size());
    size_t prefix = 0;
    while (prefix < max_common && text[prefix] == new_text[prefix]) {
      ++prefix;
    }
    size_t suffix = 0;
    while (suffix < max_common - prefix &&
           text[text.size() - 1 - suffix] == new_text[new_text.size() - 1 - suffix]) {
      ++suffix;
    }
    TSInputEdit edit;
    edit.start_byte = static_cast<uint32_t>(prefix);
    edit.old_end_byte = static_cast<uint32_t>(text.size() - suffix);
    edit.new_end_byte = static_cast<uint32_t>(new_text.size() - suffix);
    edit.start_point = ts_point_at(text, edit.start_byte);
    edit.old_end_point = ts_point_at(text, edit.old_end_byte);
    edit.new_end_point = ts_point_at(new_text, edit.new_end_byte);
    ts_tree_edit(old_tree, &edit);
    incremental = true;
  }
  text.assign(new_text);
  TSTree* new_tree =
      ts_parser_parse_string(parser, old_tree, text.data(), static_cast<uint32_t>(text.size()));
  if (old_tree) {
    ts_tree_delete(old_tree);
  }
  return new_tree;
}

}  // namespace zedit::core::detail
