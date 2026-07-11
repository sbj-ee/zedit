#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "zedit/core/editor.hpp"
#include "zedit/core/file_io.hpp"

using zedit::core::Cursor;
using zedit::core::Editor;
using zedit::core::Key;
using zedit::core::KeyEvent;
using zedit::core::Mode;

namespace {

void feed(Editor& ed, std::string_view keys) {
  for (char c : keys) {
    KeyEvent ev;
    if (c == '\x1b') {
      ev.key = Key::Escape;
    } else if (c == '\n') {
      ev.key = Key::Enter;
    } else {
      ev.key = Key::Char;
      ev.ch = c;
    }
    ed.handle_key(ev);
  }
}

void press(Editor& ed, Key key) { ed.handle_key(KeyEvent{key, 0}); }

}  // namespace

TEST_CASE("Ctrl-A selects the whole buffer as a linewise Visual selection", "[ctrl][editor]") {
  Editor ed;
  feed(ed, "iline one\nline two\nline three\x1b");
  ed.set_cursor(Cursor{1, 2});  // cursor somewhere in the middle
  press(ed, Key::CtrlA);
  REQUIRE(ed.mode() == Mode::VisualLine);
  REQUIRE(ed.visual_anchor().line == 0);
  REQUIRE(ed.visual_anchor().col == 0);
  REQUIRE(ed.cursor().line == 2);
}

TEST_CASE("Ctrl-A then d deletes the entire buffer", "[ctrl][editor]") {
  Editor ed;
  feed(ed, "iline one\nline two\x1b");
  press(ed, Key::CtrlA);
  feed(ed, "d");
  REQUIRE(ed.buffer().to_string().empty());
  REQUIRE(ed.mode() == Mode::Normal);
}

TEST_CASE("Ctrl-A works from Insert mode too", "[ctrl][editor]") {
  Editor ed;
  feed(ed, "ihello");  // still in Insert mode
  press(ed, Key::CtrlA);
  REQUIRE(ed.mode() == Mode::VisualLine);
}

TEST_CASE("Ctrl-P in Normal mode pastes like 'p'", "[ctrl][editor]") {
  Editor ed;
  feed(ed, "iline one\x1b");
  feed(ed, "yy");  // yank the line into the unnamed register
  press(ed, Key::CtrlP);
  REQUIRE(ed.buffer().to_string() == "line one\nline one");
}

TEST_CASE("Ctrl-P in Insert mode splices raw register text at the cursor",
          "[ctrl][editor]") {
  Editor ed;
  feed(ed, "iworld\x1b");   // buffer: "world"
  feed(ed, "yiw");          // yank inner word "world" into unnamed register
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "i");             // enter Insert mode at column 0
  press(ed, Key::CtrlP);
  feed(ed, "\x1b");
  REQUIRE(ed.buffer().to_string() == "worldworld");
}

TEST_CASE("Ctrl-A is ignored while typing a command line", "[ctrl][editor]") {
  Editor ed;
  feed(ed, "ihello\x1b");
  feed(ed, ":");
  press(ed, Key::CtrlA);
  REQUIRE(ed.mode() == Mode::CommandLine);
}

TEST_CASE("Ctrl-C in Normal mode copies the current line", "[ctrl][editor]") {
  Editor ed;
  feed(ed, "iline one\nline two\x1b");
  ed.set_cursor(Cursor{0, 0});
  press(ed, Key::CtrlC);
  REQUIRE(ed.unnamed_register().text == "line one\n");
  REQUIRE(ed.unnamed_register().linewise);
  // Copy never mutates the buffer.
  REQUIRE(ed.buffer().to_string() == "line one\nline two");
}

TEST_CASE("Ctrl-C in Visual mode copies the selection and exits to Normal mode",
          "[ctrl][editor]") {
  Editor ed;
  feed(ed, "ihello world\x1b");
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "v");
  feed(ed, "llll");  // select "hello" (5 chars, cursor lands on the 5th)
  press(ed, Key::CtrlC);
  REQUIRE(ed.mode() == Mode::Normal);
  REQUIRE(ed.unnamed_register().text == "hello");
  REQUIRE_FALSE(ed.unnamed_register().linewise);
  // Copy never mutates the buffer.
  REQUIRE(ed.buffer().to_string() == "hello world");
}

TEST_CASE("Ctrl-X in Normal mode cuts the current line", "[ctrl][editor]") {
  Editor ed;
  feed(ed, "iline one\nline two\x1b");
  ed.set_cursor(Cursor{0, 0});
  press(ed, Key::CtrlX);
  REQUIRE(ed.unnamed_register().text == "line one\n");
  REQUIRE(ed.unnamed_register().linewise);
  // Unlike Ctrl-C, cut removes the line from the buffer.
  REQUIRE(ed.buffer().to_string() == "line two");
}

TEST_CASE("Ctrl-X in Visual mode cuts the selection and exits to Normal mode",
          "[ctrl][editor]") {
  Editor ed;
  feed(ed, "ihello world\x1b");
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "v");
  feed(ed, "llll");  // select "hello" (5 chars, cursor lands on the 5th)
  press(ed, Key::CtrlX);
  REQUIRE(ed.mode() == Mode::Normal);
  REQUIRE(ed.unnamed_register().text == "hello");
  REQUIRE_FALSE(ed.unnamed_register().linewise);
  REQUIRE(ed.buffer().to_string() == " world");
}

TEST_CASE("Ctrl-X can be pasted right back with Ctrl-P", "[ctrl][editor]") {
  Editor ed;
  feed(ed, "iline one\nline two\x1b");
  ed.set_cursor(Cursor{0, 0});
  press(ed, Key::CtrlX);
  REQUIRE(ed.buffer().to_string() == "line two");
  press(ed, Key::CtrlP);
  REQUIRE(ed.buffer().to_string() == "line two\nline one");
}

TEST_CASE("Ctrl-S saves the buffer to disk", "[ctrl][editor]") {
  std::string path = "/tmp/zedit_test_ctrl_s_save.txt";
  Editor ed;
  ed.set_filename(path);
  feed(ed, "ihello\x1b");
  REQUIRE(ed.dirty());
  press(ed, Key::CtrlS);
  REQUIRE_FALSE(ed.dirty());
  REQUIRE(zedit::core::read_file(path) == "hello");
}

TEST_CASE("Ctrl-S surfaces a save error via last_error() rather than crashing",
          "[ctrl][editor]") {
  Editor ed;  // no filename set
  feed(ed, "ihello\x1b");
  press(ed, Key::CtrlS);
  REQUIRE_FALSE(ed.last_error().empty());
}

TEST_CASE("Ctrl-S works from Insert mode without leaving it", "[ctrl][editor]") {
  std::string path = "/tmp/zedit_test_ctrl_s_insert.txt";
  Editor ed;
  ed.set_filename(path);
  feed(ed, "i");
  ed.insert_char('h');
  press(ed, Key::CtrlS);
  REQUIRE(ed.mode() == Mode::Insert);
  REQUIRE(zedit::core::read_file(path) == "h");
}
