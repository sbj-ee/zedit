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

TEST_CASE("Enter in Insert mode copies the current line's indentation",
          "[auto-indent][editor]") {
  Editor ed;
  feed(ed, "i    if (x) {\nreturn;\x1b");
  REQUIRE(ed.buffer().to_string() == "    if (x) {\n    return;");
}

TEST_CASE("Enter on an unindented line adds no indentation", "[auto-indent][editor]") {
  Editor ed;
  feed(ed, "iplain\nline\x1b");
  REQUIRE(ed.buffer().to_string() == "plain\nline");
}

TEST_CASE("o opens a new indented line below", "[auto-indent][editor]") {
  Editor ed;
  feed(ed, "i  hello\x1b");
  feed(ed, "oworld\x1b");
  REQUIRE(ed.buffer().to_string() == "  hello\n  world");
}

TEST_CASE("O opens a new indented line above", "[auto-indent][editor]") {
  Editor ed;
  feed(ed, "i  hello\x1b");
  feed(ed, "Oworld\x1b");
  REQUIRE(ed.buffer().to_string() == "  world\n  hello");
}

TEST_CASE("auto-indent preserves tabs, not just spaces", "[auto-indent][editor]") {
  Editor ed;
  feed(ed, "i\tif (x) {\nreturn;\x1b");
  REQUIRE(ed.buffer().to_string() == "\tif (x) {\n\treturn;");
}
