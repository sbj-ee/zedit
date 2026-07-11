#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "zedit/core/editor.hpp"

using zedit::core::Cursor;
using zedit::core::Editor;
using zedit::core::Key;
using zedit::core::KeyEvent;

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

}  // namespace

TEST_CASE("G goes to the last line", "[navigation]") {
  Editor ed;
  feed(ed, "ione\ntwo\nthree\nfour\x1b");
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "G");
  REQUIRE(ed.cursor().line == 3);
  REQUIRE(ed.cursor().col == 0);
}

TEST_CASE("[count]G goes to that line", "[navigation]") {
  Editor ed;
  feed(ed, "ione\ntwo\nthree\nfour\x1b");
  ed.set_cursor(Cursor{3, 0});
  feed(ed, "2G");
  REQUIRE(ed.cursor().line == 1);  // 1-indexed like vim: 2G -> line index 1
}

TEST_CASE("G clamps an out-of-range count to the last line", "[navigation]") {
  Editor ed;
  feed(ed, "ione\ntwo\x1b");
  feed(ed, "99G");
  REQUIRE(ed.cursor().line == 1);
}

TEST_CASE("gg goes to the first line", "[navigation]") {
  Editor ed;
  feed(ed, "ione\ntwo\nthree\x1b");
  REQUIRE(ed.cursor().line == 2);
  feed(ed, "gg");
  REQUIRE(ed.cursor().line == 0);
}

TEST_CASE("[count]gg goes to that line", "[navigation]") {
  Editor ed;
  feed(ed, "ione\ntwo\nthree\nfour\x1b");
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "3gg");
  REQUIRE(ed.cursor().line == 2);
}

TEST_CASE(":N goes to line N via the command line", "[navigation]") {
  Editor ed;
  feed(ed, "ione\ntwo\nthree\nfour\x1b");
  ed.set_cursor(Cursor{0, 0});
  feed(ed, ":3\n");
  REQUIRE(ed.cursor().line == 2);
}

TEST_CASE(":N clamps an out-of-range line number", "[navigation]") {
  Editor ed;
  feed(ed, "ione\ntwo\x1b");
  feed(ed, ":9999\n");
  REQUIRE(ed.cursor().line == 1);
}
