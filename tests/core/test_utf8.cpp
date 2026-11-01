#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "zedit/core/editor.hpp"
#include "zedit/core/lsp.hpp"
#include "zedit/core/mode.hpp"
#include "zedit/core/utf8.hpp"

using namespace zedit::core;

namespace {

// "é" is 2 bytes / 1 UTF-16 unit; "😀" is 4 bytes / 2 UTF-16 units.
const std::string kE = "\xC3\xA9";
const std::string kSmile = "\xF0\x9F\x98\x80";

void feed(Editor& ed, std::string_view keys) {
  for (char c : keys) {
    KeyEvent ev;
    if (c == '\x1b') {
      ev.key = Key::Escape;
    } else if (c == '\b') {
      ev.key = Key::Backspace;
    } else {
      ev.key = Key::Char;
      ev.ch = c;
    }
    ed.handle_key(ev);
  }
}

Editor editor_with(const std::string& text, Cursor c) {
  Editor ed;
  ed.buffer() = PieceTable(text);
  ed.set_cursor(c);
  return ed;
}

}  // namespace

TEST_CASE("utf8 helpers step over whole code points", "[utf8]") {
  const std::string s = "a" + kE + kSmile + "b";  // bytes: a(0) é(1-2) 😀(3-6) b(7)
  REQUIRE(utf8_sequence_length(static_cast<unsigned char>(s[1])) == 2);
  REQUIRE(utf8_sequence_length(static_cast<unsigned char>(s[3])) == 4);
  REQUIRE(utf8_next(s, 0) == 1);
  REQUIRE(utf8_next(s, 1) == 3);
  REQUIRE(utf8_next(s, 3) == 7);
  REQUIRE(utf8_prev(s, 7) == 3);
  REQUIRE(utf8_prev(s, 3) == 1);
  REQUIRE(utf8_floor(s, 2) == 1);
  REQUIRE(utf8_floor(s, 5) == 3);
  REQUIRE(utf8_encode(0xE9) == kE);
  REQUIRE(utf8_encode(0x1F600) == kSmile);
  REQUIRE(utf8_encode('x') == "x");
}

TEST_CASE("utf8 <-> utf16 column conversion", "[utf8][lsp]") {
  const std::string s = "a" + kE + kSmile + "b";
  REQUIRE(utf8_col_to_utf16(s, 0) == 0);
  REQUIRE(utf8_col_to_utf16(s, 1) == 1);
  REQUIRE(utf8_col_to_utf16(s, 3) == 2);
  REQUIRE(utf8_col_to_utf16(s, 7) == 4);
  REQUIRE(utf8_col_to_utf16(s, s.size()) == 5);
  REQUIRE(utf16_col_to_utf8(s, 2) == 3);
  REQUIRE(utf16_col_to_utf8(s, 4) == 7);
  REQUIRE(utf16_col_to_utf8(s, 5) == s.size());
  REQUIRE(utf16_col_to_utf8(s, 99) == s.size());  // clamps past end
}

TEST_CASE("h/l move by code point, not byte", "[utf8][integration]") {
  Editor ed = editor_with("a" + kE + kSmile + "b", Cursor{0, 0});
  feed(ed, "l");
  REQUIRE(ed.cursor().col == 1);
  feed(ed, "l");
  REQUIRE(ed.cursor().col == 3);
  feed(ed, "l");
  REQUIRE(ed.cursor().col == 7);
  feed(ed, "h");
  REQUIRE(ed.cursor().col == 3);
  feed(ed, "h");
  REQUIRE(ed.cursor().col == 1);
}

TEST_CASE("x deletes whole code points, with a count", "[utf8][integration]") {
  Editor ed = editor_with("a" + kE + kSmile + "b", Cursor{0, 1});
  feed(ed, "x");
  REQUIRE(ed.buffer().to_string() == "a" + kSmile + "b");
  ed = editor_with("a" + kE + kSmile + "b", Cursor{0, 1});
  feed(ed, "2x");
  REQUIRE(ed.buffer().to_string() == "ab");
}

TEST_CASE("insert-mode backspace removes a whole code point", "[utf8][integration]") {
  Editor ed = editor_with("a" + kE + kSmile, Cursor{0, 3});
  feed(ed, "a");  // append after the emoji
  REQUIRE(ed.cursor().col == 7);
  feed(ed, "\b");
  REQUIRE(ed.buffer().to_string() == "a" + kE);
  feed(ed, "\b");
  REQUIRE(ed.buffer().to_string() == "a");
}

TEST_CASE("typed non-ASCII characters insert their UTF-8 bytes", "[utf8][integration]") {
  REQUIRE(char_key_events(0).empty());
  REQUIRE(char_key_events(0xD83Du).empty());
  REQUIRE(char_key_events(0x110000u).empty());
  REQUIRE(char_key_events('q').size() == 1);
  REQUIRE(char_key_events(0x1F600).size() == 4);
  Editor ed = editor_with("", Cursor{0, 0});
  feed(ed, "i");
  for (uint32_t cp : {0xE9u, 0x1F600u, uint32_t{'z'}}) {
    for (KeyEvent ev : char_key_events(cp)) ed.handle_key(ev);
  }
  REQUIRE(ed.buffer().to_string() == kE + kSmile + "z");
}

TEST_CASE("LSP cursor column is reported in UTF-16 units", "[utf8][lsp]") {
  Editor ed = editor_with(kSmile + "x", Cursor{0, 4});
  REQUIRE(ed.lsp_cursor_col() == 2);
}

TEST_CASE("file URIs percent-encode and round-trip", "[utf8][lsp]") {
  const std::string path = "/tmp/a b#c%d/caf" + kE + ".cpp";
  const std::string uri = lsp_path_to_uri(path);
  REQUIRE(uri == "file:///tmp/a%20b%23c%25d/caf%C3%A9.cpp");
  REQUIRE(lsp_uri_to_path(uri) == path);
  REQUIRE(lsp_uri_to_path("file://localhost/tmp/x.cpp") == "/tmp/x.cpp");
  REQUIRE(lsp_path_to_uri("/tmp/plain-file_1.cpp") == "file:///tmp/plain-file_1.cpp");
}
