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
