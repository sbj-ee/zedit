#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "zedit/core/editor.hpp"

using zedit::core::Cursor;
using zedit::core::Editor;
using zedit::core::Key;
using zedit::core::KeyEvent;
using zedit::core::PieceTable;
using zedit::core::SplitLayout;

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
    } else if (c == '\x17') {  // Ctrl-W, ASCII ETB
      ev.key = Key::CtrlW;
    } else {
      ev.key = Key::Char;
      ev.ch = c;
    }
    ed.handle_key(ev);
  }
}

}  // namespace

TEST_CASE("a fresh Editor starts with exactly one window", "[windows]") {
  Editor ed;
  REQUIRE(ed.window_count() == 1);
  REQUIRE(ed.current_window_index() == 0);
  REQUIRE(ed.split_layout() == SplitLayout::Single);
}

TEST_CASE("split_horizontal adds a stacked window focused on the new one", "[windows]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("hello"));
  ed.set_cursor(Cursor{0, 2});
  ed.split_horizontal();
  REQUIRE(ed.window_count() == 2);
  REQUIRE(ed.current_window_index() == 1);
  REQUIRE(ed.split_layout() == SplitLayout::Stacked);
  REQUIRE(ed.buffer().to_string() == "hello");  // same buffer
  REQUIRE(ed.cursor().col == 2);                // cursor copied from the split point
}

TEST_CASE("split_vertical sets side-by-side layout", "[windows]") {
  Editor ed;
  ed.split_vertical();
  REQUIRE(ed.window_count() == 2);
  REQUIRE(ed.split_layout() == SplitLayout::SideBySide);
}

TEST_CASE("windows on the same buffer have independent cursors", "[windows]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc\nd"));
  ed.set_cursor(Cursor{0, 0});
  ed.split_horizontal();
  ed.set_cursor(Cursor{3, 0});  // move only window 1's cursor

  ed.set_current_window(0);
  REQUIRE(ed.cursor().line == 0);
  ed.set_current_window(1);
  REQUIRE(ed.cursor().line == 3);
}

TEST_CASE("edits in one window are visible in another window on the same buffer", "[windows]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("hello"));
  ed.split_horizontal();  // window 1, same buffer

  feed(ed, "x");  // deletes 'h' via window 1
  REQUIRE(ed.buffer().to_string() == "ello");

  ed.set_current_window(0);
  REQUIRE(ed.buffer().to_string() == "ello");  // window 0 sees the same shared buffer
}

TEST_CASE("undo history is shared across windows viewing the same buffer", "[windows]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("hello"));
  ed.split_horizontal();

  feed(ed, "x");
  REQUIRE(ed.buffer().to_string() == "ello");

  ed.set_current_window(0);
  feed(ed, "u");  // undo from the OTHER window
  REQUIRE(ed.buffer().to_string() == "hello");
}

TEST_CASE("switching a window's buffer doesn't affect other windows", "[windows]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("buffer zero"));
  ed.split_horizontal();  // window 1 also views buffer 0

  ed.open_buffer("/tmp/zedit_test_window_buf.txt");  // window 1 -> new buffer 1
  REQUIRE(ed.buffer_count() == 2);
  REQUIRE(ed.current_buffer_index() == 1);

  ed.set_current_window(0);
  REQUIRE(ed.current_buffer_index() == 0);
  REQUIRE(ed.buffer().to_string() == "buffer zero");
}

TEST_CASE("next_window cycles focus and wraps", "[windows]") {
  Editor ed;
  ed.split_horizontal();
  ed.split_horizontal();
  REQUIRE(ed.window_count() == 3);
  REQUIRE(ed.current_window_index() == 2);

  ed.next_window();
  REQUIRE(ed.current_window_index() == 0);
  ed.next_window();
  REQUIRE(ed.current_window_index() == 1);
}

TEST_CASE("close_window removes the current window and never drops below one", "[windows]") {
  Editor ed;
  ed.split_horizontal();
  ed.split_horizontal();
  REQUIRE(ed.window_count() == 3);

  ed.close_window();
  REQUIRE(ed.window_count() == 2);

  ed.close_window();
  ed.close_window();
  REQUIRE(ed.window_count() == 1);
  REQUIRE(ed.split_layout() == SplitLayout::Single);

  ed.close_window();  // no-op, can't close the last window
  REQUIRE(ed.window_count() == 1);
}

TEST_CASE(":sp and :vsp split via the key interface", "[windows][integration]") {
  Editor ed;
  feed(ed, ":sp\n");
  REQUIRE(ed.window_count() == 2);
  REQUIRE(ed.split_layout() == SplitLayout::Stacked);

  feed(ed, ":vsp\n");
  REQUIRE(ed.window_count() == 3);
  REQUIRE(ed.split_layout() == SplitLayout::Stacked);  // orientation set by the first split
}

TEST_CASE("Ctrl-W cycles window focus via the key interface", "[windows][integration]") {
  Editor ed;
  feed(ed, ":sp\n");
  REQUIRE(ed.current_window_index() == 1);
  feed(ed, "\x17");
  REQUIRE(ed.current_window_index() == 0);
}

TEST_CASE(":close closes the current window via the key interface", "[windows][integration]") {
  Editor ed;
  feed(ed, ":sp\n");
  REQUIRE(ed.window_count() == 2);
  feed(ed, ":close\n");
  REQUIRE(ed.window_count() == 1);
}
