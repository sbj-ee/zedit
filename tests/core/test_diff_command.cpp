#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "zedit/core/editor.hpp"
#include "zedit/core/file_io.hpp"

using zedit::core::DiffLineStatus;
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
    } else {
      ev.key = Key::Char;
      ev.ch = c;
    }
    ed.handle_key(ev);
  }
}

}  // namespace

TEST_CASE("diff_with splits vertically and marks the pair for diffing", "[diff][windows]") {
  std::string path = "/tmp/zedit_test_diff_other.txt";
  zedit::core::write_file(path, "a\nx\nc");

  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc"));
  REQUIRE_FALSE(ed.is_diffing());

  ed.diff_with(path);
  REQUIRE(ed.window_count() == 2);
  REQUIRE(ed.split_layout() == SplitLayout::SideBySide);
  REQUIRE(ed.is_diffing());
  REQUIRE(ed.buffer().to_string() == "a\nx\nc");  // new window shows the diffed-in file
}

TEST_CASE("diff_status_for_window reflects each side of the comparison", "[diff][windows]") {
  std::string path = "/tmp/zedit_test_diff_other2.txt";
  zedit::core::write_file(path, "a\nx\nc");

  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc"));
  ed.diff_with(path);

  auto right_status = ed.diff_status_for_window(1);  // the new window (buffer_b)
  REQUIRE(right_status == std::vector<DiffLineStatus>{DiffLineStatus::Unchanged,
                                                        DiffLineStatus::Added,
                                                        DiffLineStatus::Unchanged});

  auto left_status = ed.diff_status_for_window(0);  // the original window (buffer_a)
  REQUIRE(left_status == std::vector<DiffLineStatus>{DiffLineStatus::Unchanged,
                                                       DiffLineStatus::Removed,
                                                       DiffLineStatus::Unchanged});
}

TEST_CASE("a window outside the diff pair returns an empty status", "[diff][windows]") {
  std::string path = "/tmp/zedit_test_diff_other3.txt";
  zedit::core::write_file(path, "a\nx\nc");

  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc"));
  ed.diff_with(path);
  ed.split_horizontal();  // window 2: still viewing buffer_b, not a NEW buffer
  ed.open_buffer("/tmp/zedit_test_diff_unrelated.txt");  // now window 2 is unrelated

  REQUIRE(ed.diff_status_for_window(2).empty());
}

TEST_CASE(":diff opens the comparison via the key interface", "[diff][integration]") {
  std::string path = "/tmp/zedit_test_diff_ex.txt";
  zedit::core::write_file(path, "line one\nline two");

  Editor ed;
  ed.buffer() = PieceTable(std::string("line one\nchanged"));
  feed(ed, ":diff " + path + "\n");

  REQUIRE(ed.is_diffing());
  REQUIRE(ed.window_count() == 2);
  REQUIRE(ed.buffer().to_string() == "line one\nline two");
}

TEST_CASE("diff updates live as the buffer is edited", "[diff][integration]") {
  std::string path = "/tmp/zedit_test_diff_live.txt";
  zedit::core::write_file(path, "hello");

  Editor ed;
  ed.buffer() = PieceTable(std::string("hello"));
  ed.diff_with(path);
  ed.set_current_window(0);
  REQUIRE(ed.diff_status_for_window(0)[0] == DiffLineStatus::Unchanged);

  feed(ed, "x");  // "hello" -> "ello" in window 0's buffer
  REQUIRE(ed.diff_status_for_window(0)[0] == DiffLineStatus::Removed);
}

TEST_CASE("diff_with called again reuses the existing pane instead of stacking a third window",
          "[diff][windows]") {
  std::string path_b = "/tmp/zedit_test_diff_reuse_b.txt";
  std::string path_c = "/tmp/zedit_test_diff_reuse_c.txt";
  zedit::core::write_file(path_b, "a\nx\nc");
  zedit::core::write_file(path_c, "a\ny\nc");

  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc"));
  ed.diff_with(path_b);
  REQUIRE(ed.window_count() == 2);

  ed.set_current_window(0);  // focus back on the original buffer
  ed.diff_with(path_c);

  REQUIRE(ed.window_count() == 2);  // still two panes, not three
  REQUIRE(ed.buffer_filename(ed.buffer_count() - 1) == path_c);

  auto right_status = ed.diff_status_for_window(1);
  REQUIRE(right_status == std::vector<DiffLineStatus>{DiffLineStatus::Unchanged,
                                                        DiffLineStatus::Added,
                                                        DiffLineStatus::Unchanged});

  // The original pane still colors correctly against the new pair --
  // this is exactly what broke before the fix, since diff_pair_ only
  // ever tracked the latest two buffers.
  auto left_status = ed.diff_status_for_window(0);
  REQUIRE(left_status == std::vector<DiffLineStatus>{DiffLineStatus::Unchanged,
                                                       DiffLineStatus::Removed,
                                                       DiffLineStatus::Unchanged});
}
