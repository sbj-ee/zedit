#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

#include "zedit/core/editor.hpp"

using zedit::core::Editor;
using zedit::core::Key;
using zedit::core::KeyEvent;

namespace {

// Same tiny DSL as test_editor.cpp's feed(), plus '\t' -> Key::Tab.
void feed(Editor& ed, std::string_view keys) {
  for (char c : keys) {
    KeyEvent ev;
    if (c == '\x1b') {
      ev.key = Key::Escape;
    } else if (c == '\n') {
      ev.key = Key::Enter;
    } else if (c == '\t') {
      ev.key = Key::Tab;
    } else {
      ev.key = Key::Char;
      ev.ch = c;
    }
    ed.handle_key(ev);
  }
}

}  // namespace

TEST_CASE("Tab in Insert mode inserts tabstop spaces (default 4)", "[config][editor]") {
  Editor ed;
  feed(ed, "i\t\x1b");
  REQUIRE(ed.buffer().to_string() == "    ");
}

TEST_CASE("set_tabstop changes how many spaces Tab inserts", "[config][editor]") {
  Editor ed;
  ed.set_tabstop(2);
  feed(ed, "i\t\x1b");
  REQUIRE(ed.buffer().to_string() == "  ");
}

TEST_CASE("set_normal_remap replays the mapped sequence for a single key",
          "[config][editor]") {
  Editor ed;
  feed(ed, "iline one\nline two\x1b");
  ed.set_normal_remap({{'Q', "dd"}});
  feed(ed, "Q");
  REQUIRE(ed.buffer().to_string().find("line two") == std::string::npos);
  REQUIRE(ed.buffer().to_string().find("line one") != std::string::npos);
}

TEST_CASE("remap replay is non-recursive (noremap semantics)", "[config][editor]") {
  Editor ed;
  feed(ed, "ihello\x1b");
  ed.set_cursor(zedit::core::Cursor{0, 0});
  // 'a' is remapped to the literal key 'x'; 'x' is *separately* remapped to
  // an insert command. If replay were recursive, pressing 'a' would chase
  // through to 'x' and re-look it up, ending in an insert. It shouldn't --
  // 'a' should just run the built-in 'x' (delete char under cursor).
  ed.set_normal_remap({{'a', "x"}, {'x', "i!\x1b"}});
  feed(ed, "a");
  REQUIRE(ed.buffer().to_string() == "ello");
}

TEST_CASE(":lua changes an option live", "[config][editor]") {
  Editor ed;
  feed(ed, ":lua zedit.set_option(\"tabstop\", 2)\n");
  REQUIRE(ed.tabstop() == 2);
}

TEST_CASE(":lua adds a normal remap live without clobbering existing ones",
          "[config][editor]") {
  Editor ed;
  ed.set_normal_remap({{'a', "x"}});
  feed(ed, ":lua zedit.map(\"n\", \"b\", \"x\")\n");

  feed(ed, "ihello world\x1b");
  ed.set_cursor(zedit::core::Cursor{0, 0});
  feed(ed, "a");  // still mapped from before the :lua call
  feed(ed, "b");  // added by the :lua call
  REQUIRE(ed.buffer().to_string() == "llo world");
}

TEST_CASE(":lua surfaces a Lua error via last_error()", "[config][editor]") {
  Editor ed;
  feed(ed, ":lua this is not lua(((\n");
  REQUIRE_FALSE(ed.last_error().empty());
}
