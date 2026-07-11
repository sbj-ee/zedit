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
    } else {
      ev.key = Key::Char;
      ev.ch = c;
    }
    ed.handle_key(ev);
  }
}

}  // namespace

TEST_CASE("replace_all replaces every occurrence", "[find-replace][editor]") {
  Editor ed;
  feed(ed, "ifoo bar foo baz foo\x1b");
  size_t count = ed.replace_all("foo", "XYZ");
  REQUIRE(count == 3);
  REQUIRE(ed.buffer().to_string() == "XYZ bar XYZ baz XYZ");
}

TEST_CASE("replace_all handles a replacement longer than the pattern", "[find-replace][editor]") {
  Editor ed;
  feed(ed, "ia a a\x1b");
  size_t count = ed.replace_all("a", "longer");
  REQUIRE(count == 3);
  REQUIRE(ed.buffer().to_string() == "longer longer longer");
}

TEST_CASE("replace_all returns 0 and doesn't touch the buffer when nothing matches",
          "[find-replace][editor]") {
  Editor ed;
  feed(ed, "ihello world\x1b");
  size_t count = ed.replace_all("nope", "X");
  REQUIRE(count == 0);
  REQUIRE(ed.buffer().to_string() == "hello world");
}

TEST_CASE("replace_all is a single undoable action", "[find-replace][editor]") {
  Editor ed;
  feed(ed, "ifoo foo foo\x1b");
  ed.replace_all("foo", "X");
  REQUIRE(ed.buffer().to_string() == "X X X");
  ed.undo();
  REQUIRE(ed.buffer().to_string() == "foo foo foo");
}

TEST_CASE("replace_current_match replaces only when the cursor sits at a match",
          "[find-replace][editor]") {
  Editor ed;
  feed(ed, "ifoo bar\x1b");
  ed.set_cursor(Cursor{0, 0});
  REQUIRE(ed.replace_current_match("foo", "XYZ"));
  REQUIRE(ed.buffer().to_string() == "XYZ bar");
}

TEST_CASE("replace_current_match returns false when the cursor isn't at a match",
          "[find-replace][editor]") {
  Editor ed;
  feed(ed, "ifoo bar\x1b");
  ed.set_cursor(Cursor{0, 4});  // sitting on "bar", not "foo"
  REQUIRE_FALSE(ed.replace_current_match("foo", "XYZ"));
  REQUIRE(ed.buffer().to_string() == "foo bar");
}
