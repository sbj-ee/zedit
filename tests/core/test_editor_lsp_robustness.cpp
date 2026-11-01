// The editor must survive a misbehaving or missing language server: these
// run against tests/data/fake_clangd/clangd (put first on PATH), so they
// don't need a real clangd.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include "zedit/core/editor.hpp"
#include "zedit/core/file_io.hpp"
#include "zedit/core/json_rpc.hpp"

using zedit::core::Editor;

namespace {

// Puts `dir` first on PATH (or makes PATH just `dir`) for the scope.
class ScopedPath {
 public:
  explicit ScopedPath(const std::string& dir, bool only = false) {
    const char* old = std::getenv("PATH");
    had_ = old != nullptr;
    if (had_) old_ = old;
    std::string value = only || !had_ ? dir : dir + ":" + old_;
    ::setenv("PATH", value.c_str(), 1);
  }
  ~ScopedPath() {
    if (had_) {
      ::setenv("PATH", old_.c_str(), 1);
    } else {
      ::unsetenv("PATH");
    }
  }

 private:
  bool had_ = false;
  std::string old_;
};

class ScopedEnv {
 public:
  ScopedEnv(const char* name, const char* value) : name_(name) {
    ::setenv(name, value, 1);
  }
  ~ScopedEnv() { ::unsetenv(name_); }

 private:
  const char* name_;
};

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

const std::string kFakeDir = std::string(ZEDIT_TEST_DATA) + "/fake_clangd";

}  // namespace

TEST_CASE("a missing clangd leaves LSP off instead of half-running", "[lsp][robustness]") {
  std::string path = "/tmp/zedit_test_lsp_missing.cpp";
  zedit::core::write_file(path, "int x;\n");
  ScopedPath only_empty("/nonexistent-zedit-path", /*only=*/true);
  Editor ed = Editor::open_file(path);
  REQUIRE_FALSE(ed.lsp_running());
  ed.insert_char('y');  // nothing to send to, nothing to crash
  REQUIRE(ed.dirty());
}

TEST_CASE("typing after the language server crashed doesn't kill the editor",
          "[lsp][robustness]") {
  std::string path = "/tmp/zedit_test_lsp_crash.cpp";
  zedit::core::write_file(path, "int x;\n");
  ScopedPath fake(kFakeDir);
  ScopedEnv crash("ZEDIT_FAKE_LSP_MODE", "crash_after_open");
  Editor ed = Editor::open_file(path);
  REQUIRE(ed.lsp_running());
  // The fake exits right after didOpen; once it's gone running() says so.
  REQUIRE(wait_until(ed, [&] { return !ed.lsp_running(); }, std::chrono::seconds(10)));
  // Each keystroke would have written a didChange into the dead pipe
  // (SIGPIPE, exit 141) before 1.8.8.
  for (int i = 0; i < 200; ++i) {
    ed.insert_char('a');
  }
  REQUIRE(ed.dirty());
}

TEST_CASE("non-ASCII paste byte by byte with LSP running doesn't throw",
          "[lsp][robustness]") {
  std::string path = "/tmp/zedit_test_lsp_utf8.cpp";
  zedit::core::write_file(path, "int x;\n");
  ScopedPath fake(kFakeDir);
  Editor ed = Editor::open_file(path);
  REQUIRE(ed.lsp_running());
  REQUIRE(wait_until(ed, [&] { return !ed.diagnostics().empty(); }, std::chrono::seconds(10)));
  // "é" = C3 A9: after the first byte the document is invalid UTF-8.
  REQUIRE_NOTHROW(ed.insert_char('\xc3'));
  REQUIRE_NOTHROW(ed.insert_char('\xa9'));
  REQUIRE(ed.lsp_running());
}

TEST_CASE("frame_message replaces invalid UTF-8 instead of throwing", "[json_rpc]") {
  nlohmann::json msg = {{"text", std::string("a\xc3")}};
  std::string framed;
  REQUIRE_NOTHROW(framed = zedit::core::frame_message(msg));
  REQUIRE(framed.find("\xef\xbf\xbd") != std::string::npos);  // U+FFFD
}

TEST_CASE("while the server is still initializing, only the latest change is queued",
          "[lsp][robustness]") {
  ScopedEnv mode("ZEDIT_FAKE_LSP_MODE", "never_initialize");
  zedit::core::LspManager lsp;
  REQUIRE(lsp.start(kFakeDir + "/clangd"));
  lsp.open_document("/tmp/zedit_test_deferred.cpp", "x");
  std::string text = "x";
  for (int i = 0; i < 1000; ++i) {
    text += 'y';
    lsp.change_document("/tmp/zedit_test_deferred.cpp", text);
  }
  // didOpen + one didChange, not 1000 full copies of the document.
  REQUIRE(lsp.deferred_count() == 2);
  lsp.close_document("/tmp/zedit_test_deferred.cpp");
  lsp.open_document("/tmp/zedit_test_deferred.cpp", "z");
  lsp.change_document("/tmp/zedit_test_deferred.cpp", "zz");
  REQUIRE(lsp.deferred_count() == 5);  // the reopen gets its own change slot
}

TEST_CASE("queued changes reach the server with the newest text", "[lsp][robustness]") {
  std::string path = "/tmp/zedit_test_lsp_latest.cpp";
  zedit::core::write_file(path, "int x;\n");
  ScopedPath fake(kFakeDir);
  Editor ed = Editor::open_file(path);
  // Typed before the handshake can have finished: queued, then flushed.
  ed.insert_char('a');
  ed.insert_char('b');
  REQUIRE(wait_until(
      ed,
      [&] {
        auto d = ed.diagnostics();
        return !d.empty() && d.front().message == "text:abint x;\n";
      },
      std::chrono::seconds(10)));
}

TEST_CASE("Insert-mode paste of a large register is a single edit", "[paste][perf]") {
  Editor ed;
  std::string big;
  for (int i = 0; i < 4000; ++i) big += "line " + std::to_string(i) + " of pasted text\n";
  ed.set_unnamed_register(big, false);
  zedit::core::KeyEvent i_key{zedit::core::Key::Char, 'i'};
  ed.handle_key(i_key);
  auto start = std::chrono::steady_clock::now();
  ed.handle_key(zedit::core::KeyEvent{zedit::core::Key::CtrlP, 0});
  auto elapsed = std::chrono::steady_clock::now() - start;
  REQUIRE(ed.buffer().to_string() == big);
  REQUIRE(ed.cursor().line == 4000);
  REQUIRE(ed.cursor().col == 0);
  // ~90 KB: took tens of seconds byte by byte; one edit is milliseconds.
  REQUIRE(elapsed < std::chrono::seconds(2));
}
