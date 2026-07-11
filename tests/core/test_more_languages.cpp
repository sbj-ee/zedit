#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "zedit/core/languages.hpp"

using zedit::core::HighlightSpan;
using zedit::core::make_highlighter_for_filename;
using zedit::core::TokenKind;

namespace {

bool any_span_covers(const std::vector<HighlightSpan>& spans, size_t start, size_t end,
                      TokenKind kind) {
  return std::any_of(spans.begin(), spans.end(), [&](const HighlightSpan& s) {
    return s.kind == kind && s.start_byte <= start && s.end_byte >= end;
  });
}

}  // namespace

TEST_CASE("make_highlighter_for_filename picks tree-sitter for Python extensions", "[languages]") {
  auto h = make_highlighter_for_filename("script.py");
  std::string code = "def f():\n    pass";
  h->set_text(code);
  REQUIRE_FALSE(h->highlight(0, code.size()).empty());
}

TEST_CASE("Python highlighter classifies keywords and comments", "[languages]") {
  auto h = make_highlighter_for_filename("a.py");
  std::string code = "# hi\ndef f():\n    return 1";
  h->set_text(code);
  auto spans = h->highlight(0, code.size());
  REQUIRE(any_span_covers(spans, 0, 4, TokenKind::Comment));
  size_t def_pos = code.find("def");
  REQUIRE(any_span_covers(spans, def_pos, def_pos + 3, TokenKind::Keyword));
  size_t ret_pos = code.find("return");
  REQUIRE(any_span_covers(spans, ret_pos, ret_pos + 6, TokenKind::Keyword));
}

TEST_CASE("Python highlighter classifies a string literal", "[languages]") {
  auto h = make_highlighter_for_filename("a.py");
  std::string code = "s = 'hello'";
  h->set_text(code);
  auto spans = h->highlight(0, code.size());
  size_t str_pos = code.find("'hello'");
  REQUIRE(any_span_covers(spans, str_pos, str_pos + 7, TokenKind::String));
}

TEST_CASE("make_highlighter_for_filename picks tree-sitter for Markdown extensions", "[languages]") {
  auto h = make_highlighter_for_filename("readme.md");
  std::string code = "# Title\n\nSome text.";
  h->set_text(code);
  REQUIRE_FALSE(h->highlight(0, code.size()).empty());
}

TEST_CASE("Markdown highlighter classifies an ATX heading as a title", "[languages]") {
  auto h = make_highlighter_for_filename("a.md");
  std::string code = "# Hello World";
  h->set_text(code);
  auto spans = h->highlight(0, code.size());
  size_t title_pos = code.find("Hello World");
  REQUIRE(any_span_covers(spans, title_pos, title_pos + 11, TokenKind::Keyword));
}

TEST_CASE("Markdown highlighter classifies a fenced code block as literal", "[languages]") {
  auto h = make_highlighter_for_filename("a.md");
  std::string code = "```\ncode here\n```";
  h->set_text(code);
  auto spans = h->highlight(0, code.size());
  REQUIRE(any_span_covers(spans, 0, code.size(), TokenKind::String));
}
