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

// Same mini-DSL as test_editor.cpp: '\x1b'=Escape, '\n'=Enter, '\b'=Backspace,
// '\x12' (Ctrl-R, ASCII DC2) = redo, everything else -> Char.
void feed(Editor& ed, std::string_view keys) {
  for (char c : keys) {
    KeyEvent ev;
    if (c == '\x1b') {
      ev.key = Key::Escape;
    } else if (c == '\n') {
      ev.key = Key::Enter;
    } else if (c == '\b') {
      ev.key = Key::Backspace;
    } else if (c == '\x12') {
      ev.key = Key::CtrlR;
    } else {
      ev.key = Key::Char;
      ev.ch = c;
    }
    ed.handle_key(ev);
  }
}

}  // namespace

TEST_CASE("dd deletes the current line", "[operators][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc"));
  ed.set_cursor(Cursor{1, 0});
  feed(ed, "dd");
  REQUIRE(ed.buffer().to_string() == "a\nc");
  REQUIRE(ed.cursor().line == 1);
}

TEST_CASE("3dd deletes three lines using a count", "[operators][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc\nd\ne"));
  ed.set_cursor(Cursor{1, 0});
  feed(ed, "3dd");
  REQUIRE(ed.buffer().to_string() == "a\ne");
}

TEST_CASE("yy then p yanks and pastes a line below", "[operators][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "yy");
  REQUIRE(ed.buffer().to_string() == "a\nb");  // yank doesn't mutate
  feed(ed, "p");
  REQUIRE(ed.buffer().to_string() == "a\na\nb");
}

TEST_CASE("yy then P pastes a line above", "[operators][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb"));
  ed.set_cursor(Cursor{1, 0});
  feed(ed, "yy");
  feed(ed, "P");
  REQUIRE(ed.buffer().to_string() == "a\nb\nb");
}

TEST_CASE("x deletes the character under the cursor", "[operators][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("abc"));
  ed.set_cursor(Cursor{0, 1});
  feed(ed, "x");
  REQUIRE(ed.buffer().to_string() == "ac");
}

TEST_CASE("dw deletes a word forward", "[operators][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar baz"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "dw");
  REQUIRE(ed.buffer().to_string() == "bar baz");
}

TEST_CASE("cw deletes a word forward and enters insert mode", "[operators][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "cwhi\x1b");
  REQUIRE(ed.buffer().to_string() == "hi bar");
  REQUIRE(ed.mode() == Mode::Normal);
}

TEST_CASE("ciw changes the word under the cursor", "[operators][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar baz"));
  ed.set_cursor(Cursor{0, 5});  // inside "bar"
  feed(ed, "ciwXYZ\x1b");
  REQUIRE(ed.buffer().to_string() == "foo XYZ baz");
}

TEST_CASE("diw deletes the word under the cursor without entering insert mode", "[operators][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar baz"));
  ed.set_cursor(Cursor{0, 5});
  feed(ed, "diw");
  REQUIRE(ed.buffer().to_string() == "foo  baz");
  REQUIRE(ed.mode() == Mode::Normal);
}

TEST_CASE("u undoes the last change and Ctrl-R redoes it", "[operators][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc"));
  ed.set_cursor(Cursor{1, 0});
  feed(ed, "dd");
  REQUIRE(ed.buffer().to_string() == "a\nc");

  feed(ed, "u");
  REQUIRE(ed.buffer().to_string() == "a\nb\nc");

  feed(ed, "\x12");
  REQUIRE(ed.buffer().to_string() == "a\nc");
}

TEST_CASE("an Insert-mode session undoes as a single unit", "[operators][integration]") {
  Editor ed;
  feed(ed, "ihello\x1b");
  REQUIRE(ed.buffer().to_string() == "hello");
  feed(ed, "u");
  REQUIRE(ed.buffer().to_string().empty());
}

TEST_CASE("undo restores the pre-edit cursor position", "[operators][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc"));
  ed.set_cursor(Cursor{2, 0});
  feed(ed, "dd");
  feed(ed, "u");
  REQUIRE(ed.cursor().line == 2);
}
