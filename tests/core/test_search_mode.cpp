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

TEST_CASE("/ searches forward and lands on the match", "[search][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar foo baz"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "/bar\n");
  REQUIRE(ed.mode() == Mode::Normal);
  REQUIRE(ed.cursor().col == 4);
}

TEST_CASE("n repeats the last search forward, N reverses it", "[search][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar foo baz foo"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "/foo\n");
  REQUIRE(ed.cursor().col == 8);
  feed(ed, "n");
  REQUIRE(ed.cursor().col == 16);
  feed(ed, "N");
  REQUIRE(ed.cursor().col == 8);
}

TEST_CASE("? searches backward", "[search][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar foo baz"));
  ed.set_cursor(Cursor{0, 12});
  feed(ed, "?foo\n");
  REQUIRE(ed.cursor().col == 8);
}

TEST_CASE("Esc cancels search without moving the cursor", "[search][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "/bar");
  REQUIRE(ed.mode() == Mode::Search);
  feed(ed, "\x1b");
  REQUIRE(ed.mode() == Mode::Normal);
  REQUIRE(ed.cursor().col == 0);
}

TEST_CASE("* searches forward for the word under the cursor", "[search][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar foo baz"));
  ed.set_cursor(Cursor{0, 9});  // inside the second "foo"
  feed(ed, "*");
  REQUIRE(ed.cursor().col == 0);  // wraps around to the first "foo"
}

TEST_CASE("# searches backward for the word under the cursor", "[search][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar foo baz"));
  ed.set_cursor(Cursor{0, 0});  // on the first "foo"
  feed(ed, "#");
  REQUIRE(ed.cursor().col == 8);  // wraps around to the second "foo"
}

TEST_CASE("pressing / then Enter with no new text repeats the last pattern", "[search][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar foo baz"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "/foo\n");
  REQUIRE(ed.cursor().col == 8);
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "/\n");
  REQUIRE(ed.cursor().col == 8);
}
