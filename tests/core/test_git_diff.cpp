#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "zedit/core/diff.hpp"
#include "zedit/core/editor.hpp"
#include "zedit/core/git_diff.hpp"

using zedit::core::DiffLineStatus;
using zedit::core::Editor;
using zedit::core::git_head_content;

namespace {

namespace fs = std::filesystem;

// Sets up a scratch git repo with one committed file, for tests to read
// back via git_head_content(). std::system() (rather than
// zedit::core::Subprocess) is fine here -- this is test fixture setup,
// not the shipped code path being tested.
fs::path make_repo_with_committed_file(const std::string& filename, const std::string& content) {
  fs::path dir = fs::temp_directory_path() / "zedit_test_git_diff_repo";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);

  fs::path file_path = dir / filename;
  std::ofstream(file_path) << content;

  std::string cmd = "cd " + dir.string() +
                     " && git init -q"
                     " && git -c user.email=test@test.com -c user.name=test add " +
                     filename +
                     " && git -c user.email=test@test.com -c user.name=test commit -q -m init";
  int rc = std::system(cmd.c_str());  // test scaffolding, not shipped code
  REQUIRE(rc == 0);
  return file_path;
}

}  // namespace

TEST_CASE("git_head_content returns a tracked file's committed content", "[git-diff]") {
  fs::path file = make_repo_with_committed_file("tracked.txt", "line one\nline two\n");
  auto content = git_head_content(file.string());
  REQUIRE(content.has_value());
  REQUIRE(*content == "line one\nline two\n");
}

TEST_CASE("git_head_content reads from HEAD, not the working tree", "[git-diff]") {
  fs::path file = make_repo_with_committed_file("tracked.txt", "original\n");
  std::ofstream(file) << "changed on disk, not committed\n";

  auto content = git_head_content(file.string());
  REQUIRE(content.has_value());
  REQUIRE(*content == "original\n");
}

TEST_CASE("git_head_content returns nullopt for a file outside any git repo", "[git-diff]") {
  fs::path dir = fs::temp_directory_path() / "zedit_test_git_diff_no_repo";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  fs::path file = dir / "untracked.txt";
  std::ofstream(file) << "hello\n";

  REQUIRE_FALSE(git_head_content(file.string()).has_value());
}

TEST_CASE("Editor::git_diff_status flags edited lines against the git HEAD version",
          "[git-diff][editor]") {
  fs::path file = make_repo_with_committed_file("tracked.txt", "a\nb\nc\n");

  Editor ed = Editor::open_file(file.string());
  ed.set_cursor(zedit::core::Cursor{1, 0});
  ed.erase_range(ed.cursor_offset(), 1);
  ed.insert_text(ed.cursor_offset(), "x");  // "a\nb\nc\n" -> "a\nx\nc\n", unsaved

  auto status = ed.git_diff_status();
  // 4 entries, not 3: a trailing "\n" gives both sides a final empty
  // line in this codebase's line-splitting convention (same reason the
  // status bar's line count is one more than `wc -l` reports).
  REQUIRE(status == std::vector<DiffLineStatus>{DiffLineStatus::Unchanged, DiffLineStatus::Added,
                                                 DiffLineStatus::Unchanged,
                                                 DiffLineStatus::Unchanged});
}

TEST_CASE("Editor::git_diff_status returns empty for a file outside any git repo",
          "[git-diff][editor]") {
  fs::path dir = fs::temp_directory_path() / "zedit_test_git_diff_editor_no_repo";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  fs::path file = dir / "untracked.txt";
  std::ofstream(file) << "hello\n";

  Editor ed = Editor::open_file(file.string());
  REQUIRE(ed.git_diff_status().empty());
}
