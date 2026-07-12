#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "zedit/core/editor.hpp"
#include "zedit/core/file_io.hpp"

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

TEST_CASE("a fresh Editor starts with exactly one buffer", "[buffers]") {
  Editor ed;
  REQUIRE(ed.buffer_count() == 1);
  REQUIRE(ed.current_buffer_index() == 0);
}

TEST_CASE("open_buffer on a new path adds and switches to a new buffer", "[buffers]") {
  std::string path = "/tmp/zedit_test_buf_new.txt";
  Editor ed;
  ed.buffer() = PieceTable(std::string("original"));
  ed.open_buffer(path);
  REQUIRE(ed.buffer_count() == 2);
  REQUIRE(ed.current_buffer_index() == 1);
  REQUIRE(ed.filename() == path);
  REQUIRE(ed.buffer().to_string().empty());
}

TEST_CASE("open_buffer on an existing file loads its content", "[buffers]") {
  std::string path = "/tmp/zedit_test_buf_existing.txt";
  zedit::core::write_file(path, "hello from disk");
  Editor ed;
  ed.open_buffer(path);
  REQUIRE(ed.buffer().to_string() == "hello from disk");
}

TEST_CASE("open_buffer on an already-open path switches instead of duplicating", "[buffers]") {
  std::string path_a = "/tmp/zedit_test_buf_a.txt";
  std::string path_b = "/tmp/zedit_test_buf_b.txt";
  Editor ed;
  ed.open_buffer(path_a);
  ed.open_buffer(path_b);
  REQUIRE(ed.buffer_count() == 3);  // default + a + b
  ed.open_buffer(path_a);
  REQUIRE(ed.buffer_count() == 3);  // no duplicate
  REQUIRE(ed.filename() == path_a);
}

TEST_CASE("next_buffer and prev_buffer cycle and wrap", "[buffers]") {
  Editor ed;
  ed.open_buffer("/tmp/zedit_test_buf_x.txt");
  ed.open_buffer("/tmp/zedit_test_buf_y.txt");
  REQUIRE(ed.current_buffer_index() == 2);

  ed.next_buffer();
  REQUIRE(ed.current_buffer_index() == 0);  // wraps

  ed.prev_buffer();
  REQUIRE(ed.current_buffer_index() == 2);  // wraps the other way
}

TEST_CASE("each buffer has independent content, cursor, and undo history", "[buffers]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("buffer zero"));
  ed.set_cursor(Cursor{0, 5});
  feed(ed, "ihi\x1b");  // inserts "hi" before the 'r': "buffer zero" -> "buffehir zero"

  ed.open_buffer("/tmp/zedit_test_buf_independent.txt");
  ed.buffer() = PieceTable(std::string("buffer one"));
  ed.set_cursor(Cursor{0, 2});

  ed.switch_to_buffer(0);
  REQUIRE(ed.buffer().to_string() == "buffehir zero");
  REQUIRE(ed.cursor().col == 7);

  feed(ed, "u");  // undo only affects buffer 0's history
  REQUIRE(ed.buffer().to_string() == "buffer zero");

  ed.switch_to_buffer(1);
  REQUIRE(ed.buffer().to_string() == "buffer one");
  REQUIRE(ed.cursor().col == 2);
}

TEST_CASE(":e opens a file into a new buffer via the key interface", "[buffers][integration]") {
  std::string path = "/tmp/zedit_test_buf_ex_edit.txt";
  zedit::core::write_file(path, "from ex edit");
  Editor ed;
  feed(ed, ":e " + path + "\n");
  REQUIRE(ed.buffer_count() == 2);
  REQUIRE(ed.filename() == path);
  REQUIRE(ed.buffer().to_string() == "from ex edit");
}

TEST_CASE(":bn and :bp switch buffers via the key interface", "[buffers][integration]") {
  Editor ed;
  ed.open_buffer("/tmp/zedit_test_buf_bn1.txt");
  ed.open_buffer("/tmp/zedit_test_buf_bn2.txt");
  REQUIRE(ed.current_buffer_index() == 2);

  feed(ed, ":bn\n");
  REQUIRE(ed.current_buffer_index() == 0);

  feed(ed, ":bp\n");
  REQUIRE(ed.current_buffer_index() == 2);
}

TEST_CASE("close_buffer removes the current buffer and lands on the next one",
          "[buffers]") {
  Editor ed;
  ed.open_buffer("/tmp/zedit_test_buf_close_a.txt");
  ed.open_buffer("/tmp/zedit_test_buf_close_b.txt");
  REQUIRE(ed.buffer_count() == 3);
  REQUIRE(ed.current_buffer_index() == 2);  // "...close_b.txt"

  ed.close_buffer();
  REQUIRE(ed.buffer_count() == 2);
  REQUIRE(ed.current_buffer_index() == 0);  // wrapped to the default buffer
}

TEST_CASE("close_buffer on the last buffer resets it to a fresh empty one", "[buffers]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("only content"));
  ed.set_filename("/tmp/zedit_test_buf_close_only.txt");
  ed.save();

  ed.close_buffer();
  REQUIRE(ed.buffer_count() == 1);
  REQUIRE(ed.buffer().to_string().empty());
  REQUIRE(ed.filename().empty());
}

TEST_CASE("close_buffer declines on a dirty buffer, same as :q", "[buffers]") {
  Editor ed;
  ed.open_buffer("/tmp/zedit_test_buf_close_dirty.txt");
  feed(ed, "iunsaved\x1b");
  REQUIRE(ed.dirty());

  ed.close_buffer();
  REQUIRE(ed.buffer_count() == 2);  // unchanged -- declined
  REQUIRE(ed.buffer().to_string() == "unsaved");
}

TEST_CASE("closing a buffer moves every window viewing it to the fallback buffer",
          "[buffers][windows]") {
  std::string path_a = "/tmp/zedit_test_buf_close_windows_a.txt";
  std::string path_b = "/tmp/zedit_test_buf_close_windows_b.txt";
  Editor ed;
  ed.open_buffer(path_a);                         // buffer 1; window 0 -> buffer 1
  ed.split_horizontal();                          // window 1, duplicate: also buffer 1
  ed.open_buffer(path_b);                         // buffer 2; window 1 (current) -> buffer 2
  ed.set_current_window(0);                       // window 0 is still on buffer 1
  REQUIRE(ed.current_buffer_index() == 1);

  ed.close_buffer();  // closes buffer 1, current window is window 0
  REQUIRE(ed.buffer_count() == 2);
  REQUIRE(ed.filename() == path_b);  // window 0 landed on the fallback buffer

  ed.set_current_window(1);
  REQUIRE(ed.filename() == path_b);  // window 1 was already there, index just shifted down
}

TEST_CASE(":bd closes the current buffer via the key interface", "[buffers][integration]") {
  Editor ed;
  ed.open_buffer("/tmp/zedit_test_buf_close_ex.txt");
  REQUIRE(ed.buffer_count() == 2);

  feed(ed, ":bd\n");
  REQUIRE(ed.buffer_count() == 1);
}

TEST_CASE("registers and search state are shared across buffers", "[buffers][integration]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("shared yank"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "yw");  // yank "shared "

  ed.open_buffer("/tmp/zedit_test_buf_shared.txt");
  ed.buffer() = PieceTable(std::string("target"));
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "p");
  REQUIRE(ed.buffer().to_string() == "tshared arget");
}
