#include <catch2/catch_test_macros.hpp>

#include <string>

#include "zedit/core/editor.hpp"
#include "zedit/core/file_io.hpp"
#include "zedit/core/highlight.hpp"

using zedit::core::Editor;
using zedit::core::PlainHighlighter;

TEST_CASE("PlainHighlighter never produces spans", "[highlight]") {
  PlainHighlighter h;
  h.set_text("int main() { return 0; }");
  REQUIRE(h.highlight(0, 25).empty());
}

TEST_CASE("Editor defaults to a highlighter that produces no spans", "[highlight]") {
  Editor ed;
  ed.buffer() = zedit::core::PieceTable(std::string("hello world"));
  REQUIRE(ed.highlighter().highlight(0, 11).empty());
}

TEST_CASE("Editor::open_file selects a real highlighter for a .cpp file", "[highlight]") {
  std::string path = "/tmp/zedit_test_highlight_open.cpp";
  zedit::core::write_file(path, "int x = 1; // c++ file");
  Editor ed = Editor::open_file(path);
  REQUIRE_FALSE(ed.highlighter().highlight(0, ed.buffer().size()).empty());
}

TEST_CASE("switching buffers switches highlighters independently", "[highlight]") {
  Editor ed;  // buffer 0: plain, no filename
  ed.buffer() = zedit::core::PieceTable(std::string("plain text"));
  REQUIRE(ed.highlighter().highlight(0, 10).empty());

  ed.open_buffer("/tmp/zedit_test_highlight_switch.cpp");
  ed.buffer() = zedit::core::PieceTable(std::string("int y = 2;"));
  ed.highlighter().set_text(ed.buffer().to_string());  // direct buffer() swap bypasses mark_dirty()
  REQUIRE_FALSE(ed.highlighter().highlight(0, ed.buffer().size()).empty());

  ed.switch_to_buffer(0);
  REQUIRE(ed.highlighter().highlight(0, 10).empty());
}

TEST_CASE(":e on a nonexistent .cpp path still gets syntax highlighting once typed",
          "[highlight][integration]") {
  Editor ed;
  ed.open_buffer("/tmp/zedit_test_highlight_new_file.cpp");
  ed.buffer() = zedit::core::PieceTable(std::string("int z = 3;"));
  ed.highlighter().set_text(ed.buffer().to_string());
  REQUIRE_FALSE(ed.highlighter().highlight(0, ed.buffer().size()).empty());
}
