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

TEST_CASE("make_highlighter_for_filename picks tree-sitter for C++ extensions", "[treesitter]") {
  auto h = make_highlighter_for_filename("foo.cpp");
  std::string code = "int x = 1;";
  h->set_text(code);
  REQUIRE_FALSE(h->highlight(0, code.size()).empty());
}

TEST_CASE("make_highlighter_for_filename falls back to plain for unknown extensions", "[treesitter]") {
  auto h = make_highlighter_for_filename("notes.txt");
  h->set_text("int x = 1;");
  REQUIRE(h->highlight(0, 10).empty());
}

TEST_CASE("C++ highlighter classifies a line comment", "[treesitter]") {
  auto h = make_highlighter_for_filename("a.cpp");
  std::string code = "// hello\nint x = 1;";
  h->set_text(code);
  auto spans = h->highlight(0, code.size());
  REQUIRE(any_span_covers(spans, 0, 8, TokenKind::Comment));
}

TEST_CASE("C++ highlighter classifies a keyword", "[treesitter]") {
  auto h = make_highlighter_for_filename("a.hpp");
  std::string code = "int main() { return 0; }";
  h->set_text(code);
  auto spans = h->highlight(0, code.size());
  size_t ret_pos = code.find("return");
  REQUIRE(any_span_covers(spans, ret_pos, ret_pos + 6, TokenKind::Keyword));
}

TEST_CASE("C++ highlighter classifies a string literal", "[treesitter]") {
  auto h = make_highlighter_for_filename("a.cpp");
  std::string code = "const char* s = \"hi\";";
  h->set_text(code);
  auto spans = h->highlight(0, code.size());
  size_t str_pos = code.find("\"hi\"");
  REQUIRE(any_span_covers(spans, str_pos, str_pos + 4, TokenKind::String));
}

TEST_CASE("C++ highlighter classifies a number literal", "[treesitter]") {
  auto h = make_highlighter_for_filename("a.cpp");
  std::string code = "int x = 42;";
  h->set_text(code);
  auto spans = h->highlight(0, code.size());
  size_t num_pos = code.find("42");
  REQUIRE(any_span_covers(spans, num_pos, num_pos + 2, TokenKind::Number));
}

TEST_CASE("highlight() scoped to a byte range omits spans outside it", "[treesitter]") {
  auto h = make_highlighter_for_filename("a.cpp");
  std::string code = "// early comment\nint late_var = 1;";
  h->set_text(code);

  size_t late_start = code.find("late_var");
  auto scoped = h->highlight(late_start, code.size());
  REQUIRE_FALSE(any_span_covers(scoped, 0, 5, TokenKind::Comment));
}

TEST_CASE("reparsing after an edit reflects the new text", "[treesitter]") {
  auto h = make_highlighter_for_filename("a.cpp");
  h->set_text("int x = 1;");
  std::string code = "// now a comment";
  h->set_text(code);
  auto spans = h->highlight(0, code.size());
  REQUIRE(any_span_covers(spans, 0, code.size(), TokenKind::Comment));
}
