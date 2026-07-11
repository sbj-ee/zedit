#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "zedit/core/editor.hpp"

using zedit::core::Editor;
using zedit::core::Key;
using zedit::core::KeyEvent;

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

TEST_CASE("sort_lines sorts the whole buffer ascending", "[sort][editor]") {
  Editor ed;
  feed(ed, "icharlie\nalpha\nbravo\x1b");
  ed.sort_lines(0, 2, false);
  REQUIRE(ed.buffer().to_string() == "alpha\nbravo\ncharlie");
}

TEST_CASE("sort_lines sorts descending when reverse is true", "[sort][editor]") {
  Editor ed;
  feed(ed, "ialpha\nbravo\ncharlie\x1b");
  ed.sort_lines(0, 2, true);
  REQUIRE(ed.buffer().to_string() == "charlie\nbravo\nalpha");
}

TEST_CASE("sort_lines sorts a sub-range, leaving other lines untouched", "[sort][editor]") {
  Editor ed;
  feed(ed, "izzz\ncharlie\nalpha\nbravo\nyyy\x1b");
  ed.sort_lines(1, 3, false);
  REQUIRE(ed.buffer().to_string() == "zzz\nalpha\nbravo\ncharlie\nyyy");
}

TEST_CASE("sort_lines preserves the no-trailing-newline convention", "[sort][editor]") {
  Editor ed;
  feed(ed, "ib\na\x1b");  // buffer: "b\na" (no trailing newline)
  ed.sort_lines(0, 1, false);
  REQUIRE(ed.buffer().to_string() == "a\nb");
}

TEST_CASE("sort_lines is a single undoable action", "[sort][editor]") {
  Editor ed;
  feed(ed, "icharlie\nalpha\nbravo\x1b");
  ed.sort_lines(0, 2, false);
  REQUIRE(ed.buffer().to_string() == "alpha\nbravo\ncharlie");
  ed.undo();
  REQUIRE(ed.buffer().to_string() == "charlie\nalpha\nbravo");
}

TEST_CASE("sort_lines clamps an out-of-range end_line", "[sort][editor]") {
  Editor ed;
  feed(ed, "icharlie\nalpha\nbravo\x1b");
  ed.sort_lines(0, 999, false);
  REQUIRE(ed.buffer().to_string() == "alpha\nbravo\ncharlie");
}
