#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include "zedit/core/file_io.hpp"
#include "zedit/core/lsp.hpp"

using zedit::core::DiagnosticSeverity;
using zedit::core::LspManager;

namespace {

// clangd's first response to a project can take a little while (parsing,
// possibly indexing). Poll with a generous timeout rather than a fixed
// sleep, so the suite runs fast when clangd is quick and doesn't flake
// when it's briefly slower (e.g. first run, cold disk cache).
template <typename Predicate>
bool wait_until(LspManager& lsp, Predicate&& done, std::chrono::seconds timeout) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    lsp.poll();
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

}  // namespace

TEST_CASE("LspManager starts clangd and reports diagnostics for a real error", "[lsp][clangd]") {
  std::string path = "/tmp/zedit_test_lsp_diag.cpp";
  zedit::core::write_file(path, "int main() {\n  undeclared_identifier;\n  return 0;\n}\n");

  LspManager lsp;
  REQUIRE(lsp.start("clangd"));

  lsp.open_document(path, zedit::core::read_file(path));

  bool got_diagnostics =
      wait_until(lsp, [&] { return !lsp.diagnostics_for(path).empty(); }, std::chrono::seconds(30));
  REQUIRE(got_diagnostics);

  auto diags = lsp.diagnostics_for(path);
  REQUIRE_FALSE(diags.empty());
  REQUIRE(diags[0].severity == DiagnosticSeverity::Error);
  REQUIRE(diags[0].start_line == 1);  // 0-indexed: the "undeclared_identifier;" line

  lsp.stop();
}

TEST_CASE("LspManager go-to-definition resolves a real symbol via clangd", "[lsp][clangd]") {
  std::string path = "/tmp/zedit_test_lsp_def.cpp";
  zedit::core::write_file(path, "int add(int a, int b) {\n  return a + b;\n}\n\nint main() {\n  return add(1, 2);\n}\n");

  LspManager lsp;
  REQUIRE(lsp.start("clangd"));
  lsp.open_document(path, zedit::core::read_file(path));

  // Let clangd parse the file first (diagnostics arriving is a reliable
  // signal that it has processed didOpen).
  wait_until(lsp, [&] { return true; }, std::chrono::seconds(2));

  std::optional<zedit::core::LspLocation> result;
  bool got_response = false;
  // Cursor on "add" in the call "add(1, 2)" on line 5 (0-indexed), column 9.
  lsp.request_definition(path, 5, 9, [&](std::optional<zedit::core::LspLocation> loc) {
    result = loc;
    got_response = true;
  });

  bool responded = wait_until(lsp, [&] { return got_response; }, std::chrono::seconds(30));
  REQUIRE(responded);
  REQUIRE(result.has_value());
  REQUIRE(result->line == 0);  // "int add(...)" is on line 0

  lsp.stop();
}

TEST_CASE("LspManager hover returns real type information via clangd", "[lsp][clangd]") {
  std::string path = "/tmp/zedit_test_lsp_hover.cpp";
  zedit::core::write_file(path, "int add(int a, int b) {\n  return a + b;\n}\n");

  LspManager lsp;
  REQUIRE(lsp.start("clangd"));
  lsp.open_document(path, zedit::core::read_file(path));
  wait_until(lsp, [&] { return true; }, std::chrono::seconds(2));

  std::optional<std::string> result;
  bool got_response = false;
  // Cursor on "add" in the function declaration, line 0, column 4.
  lsp.request_hover(path, 0, 4, [&](std::optional<std::string> hover) {
    result = hover;
    got_response = true;
  });

  bool responded = wait_until(lsp, [&] { return got_response; }, std::chrono::seconds(30));
  REQUIRE(responded);
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->empty());

  lsp.stop();
}
