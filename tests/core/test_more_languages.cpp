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

TEST_CASE("make_highlighter_for_filename picks tree-sitter for the .c extension", "[languages]") {
  auto h = make_highlighter_for_filename("foo.c");
  std::string code = "int x = 1;";
  h->set_text(code);
  REQUIRE_FALSE(h->highlight(0, code.size()).empty());
}

TEST_CASE("C highlighter classifies keywords, a comment, and a string literal", "[languages]") {
  auto h = make_highlighter_for_filename("a.c");
  std::string code = "// hi\nconst char *s = \"hello\";\nreturn 0;";
  h->set_text(code);
  auto spans = h->highlight(0, code.size());
  REQUIRE(any_span_covers(spans, 0, 5, TokenKind::Comment));
  size_t const_pos = code.find("const");
  REQUIRE(any_span_covers(spans, const_pos, const_pos + 5, TokenKind::Keyword));
  size_t str_pos = code.find("\"hello\"");
  REQUIRE(any_span_covers(spans, str_pos, str_pos + 7, TokenKind::String));
  size_t ret_pos = code.find("return");
  REQUIRE(any_span_covers(spans, ret_pos, ret_pos + 6, TokenKind::Keyword));
}

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

TEST_CASE("Markdown highlighter classifies inline bold and italic text", "[languages][markdown-inline]") {
  auto h = make_highlighter_for_filename("a.md");
  std::string code = "plain **bold** and *italic* text";
  h->set_text(code);
  auto spans = h->highlight(0, code.size());
  size_t bold_pos = code.find("bold");
  REQUIRE(any_span_covers(spans, bold_pos, bold_pos + 4, TokenKind::Keyword));
  size_t italic_pos = code.find("italic");
  REQUIRE(any_span_covers(spans, italic_pos, italic_pos + 6, TokenKind::Keyword));
}

TEST_CASE("Markdown highlighter classifies inline code as literal", "[languages][markdown-inline]") {
  auto h = make_highlighter_for_filename("a.md");
  std::string code = "see `some_code()` here";
  h->set_text(code);
  auto spans = h->highlight(0, code.size());
  size_t code_pos = code.find("some_code()");
  REQUIRE(any_span_covers(spans, code_pos, code_pos + 11, TokenKind::String));
}

TEST_CASE("Markdown highlighter classifies a link destination", "[languages][markdown-inline]") {
  auto h = make_highlighter_for_filename("a.md");
  std::string code = "[zedit](https://example.com)";
  h->set_text(code);
  auto spans = h->highlight(0, code.size());
  size_t url_pos = code.find("https://example.com");
  REQUIRE(any_span_covers(spans, url_pos, url_pos + 19, TokenKind::String));
}

TEST_CASE("Markdown inline highlighting is scoped to the requested byte range",
          "[languages][markdown-inline]") {
  // Two paragraphs, each its own (inline) region -- highlighting a range
  // covering only the first shouldn't reparse (or return spans from) the
  // second, matching how highlight() is meant to stay bounded by the
  // visible viewport regardless of document size.
  auto h = make_highlighter_for_filename("a.md");
  std::string code = "**first**\n\n**second**\n";
  h->set_text(code);
  size_t first_end = code.find("\n\n");
  auto spans = h->highlight(0, first_end);
  size_t second_pos = code.find("second");
  REQUIRE_FALSE(any_span_covers(spans, second_pos, second_pos + 6, TokenKind::Keyword));
  size_t first_pos = code.find("first");
  REQUIRE(any_span_covers(spans, first_pos, first_pos + 5, TokenKind::Keyword));
}
