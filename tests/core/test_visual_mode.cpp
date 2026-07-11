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

TEST_CASE("select_word_at_cursor selects the word under the cursor", "[visual]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("hello world"));
  ed.set_cursor(Cursor{0, 7});  // sitting inside "world"
  ed.select_word_at_cursor();
  REQUIRE(ed.mode() == Mode::Visual);
  REQUIRE(ed.visual_anchor().line == 0);
  REQUIRE(ed.visual_anchor().col == 6);
  REQUIRE(ed.cursor().line == 0);
  REQUIRE(ed.cursor().col == 10);
  feed(ed, "d");
  REQUIRE(ed.buffer().to_string() == "hello ");
}

TEST_CASE("select_word_at_cursor selects a punctuation run, not the surrounding words",
          "[visual]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo::bar"));
  ed.set_cursor(Cursor{0, 3});  // sitting on the first ':'
  ed.select_word_at_cursor();
  REQUIRE(ed.visual_anchor().line == 0);
  REQUIRE(ed.visual_anchor().col == 3);
  REQUIRE(ed.cursor().line == 0);
  REQUIRE(ed.cursor().col == 4);
}

TEST_CASE("select_word_at_cursor on an empty buffer is a no-op", "[visual]") {
  Editor ed;
  ed.select_word_at_cursor();
  REQUIRE(ed.mode() == Mode::Normal);
}

TEST_CASE("v enters Visual mode and Esc cancels without changes", "[visual]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("hello"));
  feed(ed, "v");
  REQUIRE(ed.mode() == Mode::Visual);
  feed(ed, "\x1b");
  REQUIRE(ed.mode() == Mode::Normal);
  REQUIRE(ed.buffer().to_string() == "hello");
}

TEST_CASE("Visual d deletes the selected charwise range inclusively", "[visual]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar baz"));
  ed.set_cursor(Cursor{0, 4});  // 'b' of "bar"
  feed(ed, "v");
  feed(ed, "ll");  // extend selection to 'r' of "bar" (cols 4..6, inclusive)
  feed(ed, "d");
  REQUIRE(ed.buffer().to_string() == "foo  baz");
  REQUIRE(ed.mode() == Mode::Normal);
}

TEST_CASE("Visual selection works regardless of which end the cursor is on", "[visual]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar baz"));
  ed.set_cursor(Cursor{0, 6});  // 'r' of "bar"
  feed(ed, "v");
  feed(ed, "hh");  // extend selection backward to 'b' of "bar"
  feed(ed, "d");
  REQUIRE(ed.buffer().to_string() == "foo  baz");
}

TEST_CASE("Visual y yanks the selection without deleting it", "[visual]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar baz"));
  ed.set_cursor(Cursor{0, 4});
  feed(ed, "v");
  feed(ed, "ll");
  feed(ed, "y");
  REQUIRE(ed.buffer().to_string() == "foo bar baz");
  REQUIRE(ed.unnamed_register().text == "bar");
  feed(ed, "p");
  REQUIRE(ed.buffer().to_string() == "foo bbarar baz");
}

TEST_CASE("Visual c changes the selection and enters insert mode", "[visual]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar baz"));
  ed.set_cursor(Cursor{0, 4});
  feed(ed, "v");
  feed(ed, "ll");
  feed(ed, "cXYZ\x1b");
  REQUIRE(ed.buffer().to_string() == "foo XYZ baz");
  REQUIRE(ed.mode() == Mode::Normal);
}

TEST_CASE("V enters VisualLine mode and d deletes whole lines", "[visual]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc\nd"));
  ed.set_cursor(Cursor{1, 0});
  feed(ed, "V");
  REQUIRE(ed.mode() == Mode::VisualLine);
  feed(ed, "j");  // extend to line 2 ("c")
  feed(ed, "d");
  REQUIRE(ed.buffer().to_string() == "a\nd");
  REQUIRE(ed.mode() == Mode::Normal);
}

TEST_CASE("VisualLine selection works when the cursor moved above the anchor", "[visual]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc\nd"));
  ed.set_cursor(Cursor{2, 0});
  feed(ed, "V");
  feed(ed, "k");  // extend upward to line 1 ("b")
  feed(ed, "d");
  REQUIRE(ed.buffer().to_string() == "a\nd");
}
