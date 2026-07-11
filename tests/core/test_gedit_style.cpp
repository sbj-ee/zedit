#include <catch2/catch_test_macros.hpp>

#include <string>

#include "zedit/core/editor.hpp"
#include "zedit/core/piece_table.hpp"

using zedit::core::Cursor;
using zedit::core::Editor;
using zedit::core::EditingStyle;
using zedit::core::Key;
using zedit::core::KeyEvent;
using zedit::core::Mode;
using zedit::core::PieceTable;

namespace {

void press(Editor& ed, Key key) { ed.handle_key(KeyEvent{key, 0}); }

void type_char(Editor& ed, char c) { ed.handle_key(KeyEvent{Key::Char, c}); }

void type_text(Editor& ed, std::string_view text) {
  for (char c : text) {
    type_char(ed, c);
  }
}

}  // namespace

TEST_CASE("Gedit style defaults to off -- vim command grammar still applies", "[gedit-style]") {
  Editor ed;
  REQUIRE(ed.editing_style() == EditingStyle::Vim);
}

TEST_CASE("Switching to Gedit style drops into Insert mode", "[gedit-style]") {
  Editor ed;
  ed.set_editing_style(EditingStyle::Gedit);
  REQUIRE(ed.mode() == Mode::Insert);
}

TEST_CASE("In Gedit style, a letter key inserts immediately -- no Normal-mode command grammar",
          "[gedit-style]") {
  Editor ed;
  ed.set_editing_style(EditingStyle::Gedit);
  // 'd' would start a delete-operator sequence in vim style; in Gedit
  // style it's just the letter "d" being typed.
  type_text(ed, "dd hello");
  REQUIRE(ed.buffer().to_string() == "dd hello");
}

TEST_CASE("In Gedit style, Backspace and Enter behave like a plain text box", "[gedit-style]") {
  Editor ed;
  ed.set_editing_style(EditingStyle::Gedit);
  type_text(ed, "hello");
  press(ed, Key::Backspace);
  press(ed, Key::Enter);
  type_text(ed, "world");
  REQUIRE(ed.buffer().to_string() == "hell\nworld");
}

TEST_CASE("Shift+Right starts a selection that Escape then collapses", "[gedit-style]") {
  Editor ed;
  ed.set_editing_style(EditingStyle::Gedit);
  type_text(ed, "hello");
  ed.set_cursor(Cursor{0, 0});
  press(ed, Key::ShiftRight);
  press(ed, Key::ShiftRight);
  REQUIRE(ed.mode() == Mode::Visual);
  press(ed, Key::Escape);
  REQUIRE(ed.mode() == Mode::Insert);
  REQUIRE(ed.buffer().to_string() == "hello");  // Escape only deselects, never deletes
}

TEST_CASE("Typing over a Shift+Arrow selection replaces it", "[gedit-style]") {
  Editor ed;
  ed.set_editing_style(EditingStyle::Gedit);
  type_text(ed, "hello world");
  ed.set_cursor(Cursor{0, 0});
  // Visual-mode selection is inclusive of the cursor's landing
  // character (matching vim's own "v" + motions, which this reuses --
  // see the existing "llll selects 5 chars" precedent elsewhere in this
  // suite), so 4 presses from col 0 selects cols [0,4] = "hello".
  for (int i = 0; i < 4; ++i) {
    press(ed, Key::ShiftRight);  // select "hello"
  }
  type_char(ed, 'X');
  REQUIRE(ed.buffer().to_string() == "X world");
  REQUIRE(ed.mode() == Mode::Insert);
}

TEST_CASE("Backspace with an active selection removes only the selection", "[gedit-style]") {
  Editor ed;
  ed.set_editing_style(EditingStyle::Gedit);
  type_text(ed, "hello world");
  ed.set_cursor(Cursor{0, 0});
  for (int i = 0; i < 4; ++i) {
    press(ed, Key::ShiftRight);  // select "hello"
  }
  press(ed, Key::Backspace);
  REQUIRE(ed.buffer().to_string() == " world");
  REQUIRE(ed.mode() == Mode::Insert);
}

TEST_CASE("A plain arrow key with an active selection deselects and moves", "[gedit-style]") {
  Editor ed;
  ed.set_editing_style(EditingStyle::Gedit);
  type_text(ed, "hello world");
  ed.set_cursor(Cursor{0, 0});
  for (int i = 0; i < 5; ++i) {
    press(ed, Key::ShiftRight);  // select "hello", cursor lands at col 5
  }
  press(ed, Key::Right);  // deselect and move one more right
  REQUIRE(ed.mode() == Mode::Insert);
  type_char(ed, 'X');
  REQUIRE(ed.buffer().to_string() == "hello Xworld");
}

TEST_CASE("Ctrl-C with a selection copies it without deleting", "[gedit-style]") {
  Editor ed;
  ed.set_editing_style(EditingStyle::Gedit);
  type_text(ed, "hello world");
  ed.set_cursor(Cursor{0, 0});
  for (int i = 0; i < 4; ++i) {
    press(ed, Key::ShiftRight);  // select "hello"
  }
  press(ed, Key::CtrlC);
  REQUIRE(ed.buffer().to_string() == "hello world");
  REQUIRE(ed.unnamed_register().text == "hello");
  press(ed, Key::Escape);
  ed.set_cursor(Cursor{0, 11});
  press(ed, Key::CtrlP);
  REQUIRE(ed.buffer().to_string() == "hello worldhello");
}

TEST_CASE("Ctrl-X with a selection cuts it", "[gedit-style]") {
  Editor ed;
  ed.set_editing_style(EditingStyle::Gedit);
  type_text(ed, "hello world");
  ed.set_cursor(Cursor{0, 0});
  for (int i = 0; i < 5; ++i) {
    press(ed, Key::ShiftRight);  // select "hello "
  }
  press(ed, Key::CtrlX);
  REQUIRE(ed.buffer().to_string() == "world");
  REQUIRE(ed.unnamed_register().text == "hello ");
  REQUIRE(ed.mode() == Mode::Insert);
}

TEST_CASE("Ctrl-A selects the whole buffer in Gedit style, typing replaces everything",
          "[gedit-style]") {
  Editor ed;
  ed.set_editing_style(EditingStyle::Gedit);
  type_text(ed, "line one\nline two");
  press(ed, Key::CtrlA);
  REQUIRE(ed.mode() == Mode::VisualLine);
  type_char(ed, 'X');
  REQUIRE(ed.buffer().to_string() == "X");
}

TEST_CASE("Switching back to Vim style restores modal command grammar", "[gedit-style]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("hello world"));
  ed.set_editing_style(EditingStyle::Gedit);
  ed.set_editing_style(EditingStyle::Vim);
  REQUIRE(ed.editing_style() == EditingStyle::Vim);
  ed.set_cursor(Cursor{0, 0});
  // Back in vim style, "dw" deletes a word again instead of being typed
  // as literal text.
  press(ed, Key::Escape);  // land cleanly in Normal mode
  type_char(ed, 'd');
  type_char(ed, 'w');
  REQUIRE(ed.buffer().to_string() == "world");
}
