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
using zedit::core::PieceTable;

namespace {

// A tiny DSL for driving Editor::handle_key from a literal string:
// '\x1b' -> Escape, '\n' -> Enter, '\b' -> Backspace, anything else -> Char.
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

TEST_CASE("i enters insert mode and types text", "[editor]") {
  Editor ed;
  feed(ed, "ihello\x1b");
  REQUIRE(ed.buffer().to_string() == "hello");
  REQUIRE(ed.mode() == Mode::Normal);
  REQUIRE(ed.cursor().col == 4);
}

TEST_CASE("a appends after the character under the cursor", "[editor]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("abc"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "aX\x1b");
  REQUIRE(ed.buffer().to_string() == "aXbc");
  REQUIRE(ed.cursor().col == 2);
}

TEST_CASE("a on the last character can append past the end", "[editor]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("abc"));
  ed.set_cursor(Cursor{0, 2});
  feed(ed, "aX\x1b");
  REQUIRE(ed.buffer().to_string() == "abcX");
}

TEST_CASE("o opens a new line below and enters insert mode", "[editor]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("abc"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "oxyz\x1b");
  REQUIRE(ed.buffer().to_string() == "abc\nxyz");
  REQUIRE(ed.cursor().line == 1);
  REQUIRE(ed.cursor().col == 2);
}

TEST_CASE("O opens a new line above and enters insert mode", "[editor]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("abc"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "OX\x1b");
  REQUIRE(ed.buffer().to_string() == "X\nabc");
  REQUIRE(ed.cursor().line == 0);
}

TEST_CASE("backspace deletes the character before the cursor", "[editor]") {
  Editor ed;
  feed(ed, "iabc\b\x1b");
  REQUIRE(ed.buffer().to_string() == "ab");
}

TEST_CASE("backspace at column 0 joins with the previous line", "[editor]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("abc\ndef"));
  ed.set_cursor(Cursor{1, 0});
  feed(ed, "i\bX\x1b");
  REQUIRE(ed.buffer().to_string() == "abcXdef");
  REQUIRE(ed.cursor().line == 0);
}

TEST_CASE("l does not move past the last character in Normal mode", "[editor]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("abc"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "llll");
  REQUIRE(ed.cursor().col == 2);
}

TEST_CASE("h does not move past column 0", "[editor]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("abc"));
  ed.set_cursor(Cursor{0, 2});
  feed(ed, "hhhh");
  REQUIRE(ed.cursor().col == 0);
}

TEST_CASE("j clamps the column to the shorter destination line", "[editor]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("abcdef\nxy"));
  ed.set_cursor(Cursor{0, 5});
  feed(ed, "j");
  REQUIRE(ed.cursor().line == 1);
  REQUIRE(ed.cursor().col == 1);
}

TEST_CASE("k clamps the column to the shorter destination line", "[editor]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("ab\nabcdef"));
  ed.set_cursor(Cursor{1, 5});
  feed(ed, "k");
  REQUIRE(ed.cursor().line == 0);
  REQUIRE(ed.cursor().col == 1);
}

TEST_CASE("j and k do not move past the first or last line", "[editor]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "k");
  REQUIRE(ed.cursor().line == 0);

  ed.set_cursor(Cursor{2, 0});
  feed(ed, "j");
  REQUIRE(ed.cursor().line == 2);
}

TEST_CASE("Editor::open_file loads file content", "[editor]") {
  std::string path = "/tmp/zedit_test_open.txt";
  zedit::core::write_file(path, "line1\nline2");
  Editor ed = Editor::open_file(path);
  REQUIRE(ed.buffer().to_string() == "line1\nline2");
  REQUIRE(ed.filename() == path);
  REQUIRE(ed.dirty() == false);
}

TEST_CASE(":w saves the buffer to disk", "[editor]") {
  std::string path = "/tmp/zedit_test_save.txt";
  Editor ed;
  ed.set_filename(path);
  feed(ed, "ihello\x1b");
  REQUIRE(ed.dirty());
  feed(ed, ":w\n");
  REQUIRE(ed.dirty() == false);
  REQUIRE(zedit::core::read_file(path) == "hello");
  REQUIRE(ed.should_quit() == false);
}

TEST_CASE(":q refuses to quit while dirty, :q! forces it", "[editor]") {
  Editor ed;
  feed(ed, "ihello\x1b");
  feed(ed, ":q\n");
  REQUIRE(ed.should_quit() == false);
  feed(ed, ":q!\n");
  REQUIRE(ed.should_quit());
}

TEST_CASE(":wq saves and quits", "[editor]") {
  std::string path = "/tmp/zedit_test_wq.txt";
  Editor ed;
  ed.set_filename(path);
  feed(ed, "ihi\x1b");
  feed(ed, ":wq\n");
  REQUIRE(ed.should_quit());
  REQUIRE(zedit::core::read_file(path) == "hi");
}

TEST_CASE("Esc cancels the command line without executing it", "[editor]") {
  Editor ed;
  feed(ed, ":q");
  REQUIRE(ed.mode() == Mode::CommandLine);
  feed(ed, "\x1b");
  REQUIRE(ed.mode() == Mode::Normal);
  REQUIRE(ed.should_quit() == false);
}

TEST_CASE("backspace on an empty command line cancels to Normal mode", "[editor]") {
  Editor ed;
  feed(ed, ":");
  REQUIRE(ed.mode() == Mode::CommandLine);
  feed(ed, "\b");
  REQUIRE(ed.mode() == Mode::Normal);
}

TEST_CASE("an unknown ex command is ignored without crashing", "[editor]") {
  Editor ed;
  feed(ed, ":bogus\n");
  REQUIRE(ed.mode() == Mode::Normal);
  REQUIRE(ed.should_quit() == false);
}
