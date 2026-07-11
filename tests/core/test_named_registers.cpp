#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "zedit/core/editor.hpp"

using zedit::core::Cursor;
using zedit::core::Editor;
using zedit::core::Key;
using zedit::core::KeyEvent;
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

TEST_CASE("register-a yank also updates the unnamed register",
          "[registers][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("first\nsecond"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "yy");  // unnamed register now holds "first\n"
  feed(ed, "j");
  feed(ed, "\"ayy");  // register a (and unnamed) now hold "second\n"
  REQUIRE(ed.register_content('a').text == "second\n");
  REQUIRE(ed.unnamed_register().text == "second\n");
}

TEST_CASE("register-a paste pulls from the named register", "[registers][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "\"ayy");
  feed(ed, "j");
  feed(ed, "\"ap");
  REQUIRE(ed.buffer().to_string() == "a\nb\na");
}

TEST_CASE("registers are independent of each other", "[registers][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("one\ntwo\nthree"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "\"ayy");
  feed(ed, "j");
  feed(ed, "\"byy");
  REQUIRE(ed.register_content('a').text == "one\n");
  REQUIRE(ed.register_content('b').text == "two\n");
}

TEST_CASE("a register prefix followed by an invalid name aborts cleanly", "[registers][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("hello"));
  feed(ed, "\"5");  // '5' is not a-z; aborts the pending register, no edit happens
  REQUIRE(ed.buffer().to_string() == "hello");
  REQUIRE(ed.mode() == zedit::core::Mode::Normal);
}

TEST_CASE("count and register prefix compose in either order", "[registers][integration]") {
  Editor ed1;
  ed1.buffer() = PieceTable(std::string("a\nb\nc\nd"));
  feed(ed1, "\"a2yy");  // register-then-count
  REQUIRE(ed1.register_content('a').text == "a\nb\n");

  Editor ed2;
  ed2.buffer() = PieceTable(std::string("a\nb\nc\nd"));
  feed(ed2, "2\"ayy");  // count-then-register
  REQUIRE(ed2.register_content('a').text == "a\nb\n");
}
