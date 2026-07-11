#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "zedit/core/editor.hpp"
#include "zedit/core/motion.hpp"

using zedit::core::Cursor;
using zedit::core::Editor;
using zedit::core::is_bracket_char;
using zedit::core::Key;
using zedit::core::KeyEvent;
using zedit::core::motion_matching_bracket;
using zedit::core::PieceTable;

namespace {

void feed(Editor& ed, std::string_view keys) {
  for (char c : keys) {
    KeyEvent ev;
    if (c == '\x1b') {
      ev.key = Key::Escape;
    } else {
      ev.key = Key::Char;
      ev.ch = c;
    }
    ed.handle_key(ev);
  }
}

}  // namespace

TEST_CASE("is_bracket_char recognizes all six bracket characters", "[bracket-match]") {
  REQUIRE(is_bracket_char('('));
  REQUIRE(is_bracket_char(')'));
  REQUIRE(is_bracket_char('{'));
  REQUIRE(is_bracket_char('}'));
  REQUIRE(is_bracket_char('['));
  REQUIRE(is_bracket_char(']'));
  REQUIRE_FALSE(is_bracket_char('a'));
  REQUIRE_FALSE(is_bracket_char(' '));
}

TEST_CASE("motion_matching_bracket finds the match from the opening bracket",
          "[bracket-match]") {
  PieceTable buf(std::string("foo(bar)baz"));
  auto match = motion_matching_bracket(buf, 3);  // offset of '('
  REQUIRE(match.has_value());
  REQUIRE(*match == 7);  // offset of ')'
}

TEST_CASE("motion_matching_bracket finds the match from the closing bracket",
          "[bracket-match]") {
  PieceTable buf(std::string("foo(bar)baz"));
  auto match = motion_matching_bracket(buf, 7);  // offset of ')'
  REQUIRE(match.has_value());
  REQUIRE(*match == 3);  // offset of '('
}

TEST_CASE("motion_matching_bracket handles nesting", "[bracket-match]") {
  PieceTable buf(std::string("a(b(c)d)e"));
  auto match = motion_matching_bracket(buf, 1);  // outer '('
  REQUIRE(match.has_value());
  REQUIRE(*match == 7);  // outer ')'
}

TEST_CASE("motion_matching_bracket searches forward on the line when not on a bracket",
          "[bracket-match]") {
  PieceTable buf(std::string("xx(yy)"));
  auto match = motion_matching_bracket(buf, 0);  // on 'x', bracket is later on the line
  REQUIRE(match.has_value());
  REQUIRE(*match == 5);  // ')'
}

TEST_CASE("motion_matching_bracket returns nullopt when there's no bracket on the line",
          "[bracket-match]") {
  PieceTable buf(std::string("no brackets here"));
  REQUIRE_FALSE(motion_matching_bracket(buf, 0).has_value());
}

TEST_CASE("motion_matching_bracket doesn't search past the current line", "[bracket-match]") {
  PieceTable buf(std::string("aaa\n(bbb)"));
  REQUIRE_FALSE(motion_matching_bracket(buf, 0).has_value());
}

TEST_CASE("'%' in Normal mode jumps the cursor to the matching bracket", "[bracket-match][editor]") {
  Editor ed;
  feed(ed, "ifoo(bar)baz\x1b");
  ed.set_cursor(Cursor{0, 3});  // on '('
  feed(ed, "%");
  REQUIRE(ed.cursor().col == 7);  // on ')'
}

TEST_CASE("'d%' deletes from an opening bracket through its match", "[bracket-match][editor]") {
  Editor ed;
  feed(ed, "ifoo(bar)baz\x1b");
  ed.set_cursor(Cursor{0, 3});  // on '('
  feed(ed, "d%");
  REQUIRE(ed.buffer().to_string() == "foobaz");
}
