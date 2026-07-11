#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "zedit/core/editor.hpp"

using zedit::core::Cursor;
using zedit::core::Editor;
using zedit::core::Key;
using zedit::core::KeyEvent;
using zedit::core::Mode;
using zedit::core::PieceTable;

namespace {

void feed(Editor& ed, std::string_view keys) {
  for (char c : keys) {
    KeyEvent ev;
    if (c == '\x1b') {
      ev.key = Key::Escape;
    } else if (c == '\n') {
      ev.key = Key::Enter;
    } else if (c == '\b') {
      ev.key = Key::Backspace;
    } else {
      ev.key = Key::Char;
      ev.ch = c;
    }
    ed.handle_key(ev);
  }
}

}  // namespace

TEST_CASE(". repeats x at the new cursor position", "[dot_repeat]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("abcde"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "x");   // "bcde", cursor stays at 0
  feed(ed, ".");   // deletes 'b'
  REQUIRE(ed.buffer().to_string() == "cde");
}

TEST_CASE(". repeats dw at the current cursor", "[dot_repeat]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("one two three"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "dw");  // "two three"
  REQUIRE(ed.buffer().to_string() == "two three");
  feed(ed, ".");   // "three"
  REQUIRE(ed.buffer().to_string() == "three");
}

TEST_CASE(". repeats dd on the new current line", "[dot_repeat]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc\nd"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "dd");  // removes "a"
  REQUIRE(ed.buffer().to_string() == "b\nc\nd");
  feed(ed, ".");   // cursor now on "b" (the new line 0); removes it too
  REQUIRE(ed.buffer().to_string() == "c\nd");
}

TEST_CASE(". repeats p at the new cursor position", "[dot_repeat]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "yy");  // yank "a\n"
  feed(ed, "p");   // paste below line 0: a,a,b,c
  REQUIRE(ed.buffer().to_string() == "a\na\nb\nc");
  feed(ed, ".");   // paste again below current line
  REQUIRE(ed.buffer().to_string() == "a\na\na\nb\nc");
}

TEST_CASE(". repeats ciw with the same typed text on a different word", "[dot_repeat]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar baz"));
  ed.set_cursor(Cursor{0, 5});  // inside "bar"
  feed(ed, "ciwXY\x1b");
  REQUIRE(ed.buffer().to_string() == "foo XY baz");
  REQUIRE(ed.mode() == Mode::Normal);

  // move onto "baz" and repeat
  ed.set_cursor(Cursor{0, 7});
  feed(ed, ".");
  REQUIRE(ed.buffer().to_string() == "foo XY XY");
}

TEST_CASE(". repeats a plain insert session at the new cursor", "[dot_repeat]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("ab\ncd"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "i123\x1b");
  REQUIRE(ed.buffer().to_string() == "123ab\ncd");
  ed.set_cursor(Cursor{1, 0});
  feed(ed, ".");
  REQUIRE(ed.buffer().to_string() == "123ab\n123cd");
}

TEST_CASE(". repeats o (open line below) with the typed text", "[dot_repeat]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "oX\x1b");
  REQUIRE(ed.buffer().to_string() == "a\nX\nb");
  feed(ed, ".");
  REQUIRE(ed.buffer().to_string() == "a\nX\nX\nb");
}

TEST_CASE("yank is not dot-repeatable", "[dot_repeat]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("abc"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "x");   // records a dot-repeatable DeleteChar: "bc"
  feed(ed, "yy");  // yank shouldn't overwrite the dot-repeat record
  feed(ed, ".");   // should still repeat the earlier 'x', not a no-op or crash
  REQUIRE(ed.buffer().to_string() == "c");
}

TEST_CASE("count is preserved across dot-repeat", "[dot_repeat]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("abcdef"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "2x");  // deletes "ab" -> "cdef"
  REQUIRE(ed.buffer().to_string() == "cdef");
  feed(ed, ".");   // deletes 2 more chars -> "ef"
  REQUIRE(ed.buffer().to_string() == "ef");
}

TEST_CASE(". with no prior change is a no-op", "[dot_repeat]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("abc"));
  feed(ed, ".");
  REQUIRE(ed.buffer().to_string() == "abc");
}
