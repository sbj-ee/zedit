#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <thread>

#include "zedit/core/editor.hpp"
#include "zedit/core/file_io.hpp"

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

template <typename Predicate>
bool wait_until(Editor& ed, Predicate&& done, std::chrono::seconds timeout) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    ed.poll_lsp();
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

}  // namespace

TEST_CASE("opening a .cpp file starts clangd and reports diagnostics", "[lsp][editor]") {
  std::string path = "/tmp/zedit_test_editor_lsp_diag.cpp";
  zedit::core::write_file(path, "int main() {\n  totally_undeclared_thing;\n}\n");

  Editor ed = Editor::open_file(path);
  bool got = wait_until(ed, [&] { return !ed.diagnostics().empty(); }, std::chrono::seconds(30));
  REQUIRE(got);
  REQUIRE_FALSE(ed.diagnostics().empty());
}

TEST_CASE("opening a non-C++ file never starts clangd", "[lsp][editor]") {
  std::string path = "/tmp/zedit_test_editor_lsp_plain.txt";
  zedit::core::write_file(path, "just some text");

  Editor ed = Editor::open_file(path);
  ed.poll_lsp();
  REQUIRE_FALSE(ed.lsp_running());
}

TEST_CASE("'gd' jumps to a real definition via clangd", "[lsp][editor][integration]") {
  std::string path = "/tmp/zedit_test_editor_lsp_gd.cpp";
  zedit::core::write_file(
      path, "int add(int a, int b) {\n  return a + b;\n}\n\nint main() {\n  return add(1, 2);\n}\n");

  Editor ed = Editor::open_file(path);
  wait_until(ed, [&] { return true; }, std::chrono::seconds(2));

  ed.set_cursor(zedit::core::Cursor{5, 9});  // on "add" in the call
  feed(ed, "gd");

  bool jumped = wait_until(ed, [&] { return ed.cursor().line == 0; }, std::chrono::seconds(30));
  REQUIRE(jumped);
}

TEST_CASE("'K' fetches hover text via clangd", "[lsp][editor][integration]") {
  std::string path = "/tmp/zedit_test_editor_lsp_hover.cpp";
  zedit::core::write_file(path, "int add(int a, int b) {\n  return a + b;\n}\n");

  Editor ed = Editor::open_file(path);
  wait_until(ed, [&] { return true; }, std::chrono::seconds(2));

  ed.set_cursor(zedit::core::Cursor{0, 4});  // on "add" in the declaration
  feed(ed, "K");

  bool got_hover =
      wait_until(ed, [&] { return ed.hover_text().has_value(); }, std::chrono::seconds(30));
  REQUIRE(got_hover);
  REQUIRE_FALSE(ed.hover_text()->empty());
}
