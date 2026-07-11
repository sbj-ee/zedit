#pragma once

#include <string_view>

#include "zedit/core/highlight.hpp"

namespace zedit::core {

// Shared by TreeSitterHighlighter and MarkdownHighlighter (which needs it
// twice -- once for the block grammar's captures, once for the inline
// grammar's) rather than duplicated, so a new capture name only ever
// needs mapping in one place.
//
// Capture names carry dotted suffixes ("function.special",
// "variable.builtin", ...); usually only the first segment decides the
// color. Markdown's "text.*" captures are the exception -- title/literal/
// uri/reference/emphasis/strong mean different things and need distinct
// (or at least deliberate) colors, so they're matched on the full name
// before falling through to the generic rule.
inline TokenKind capture_name_to_kind(std::string_view name) {
  if (name == "text.title") return TokenKind::Keyword;
  if (name == "text.literal") return TokenKind::String;
  if (name == "text.uri") return TokenKind::String;
  if (name == "text.reference") return TokenKind::Constant;
  // No dedicated "emphasis" color in TokenKind -- Keyword reads as "this
  // text is doing something other than being plain prose," which is the
  // right visual weight for bold/italic without adding a new kind (and
  // matching theme.cpp color) for a single markdown-only case.
  if (name == "text.emphasis") return TokenKind::Keyword;
  if (name == "text.strong") return TokenKind::Keyword;

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

}  // namespace zedit::core
