// Saving must never lose data: a failed write throws, keeps the buffer
// dirty and leaves the original file as it was; and :q / :wq won't quit
// while another buffer has unsaved changes.
#include <catch2/catch_test_macros.hpp>

#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <string_view>

#include "zedit/core/editor.hpp"
#include "zedit/core/file_io.hpp"

using zedit::core::Editor;
using zedit::core::FileIoError;
using zedit::core::Key;
using zedit::core::KeyEvent;
using zedit::core::read_file;
using zedit::core::write_file;
namespace fs = std::filesystem;

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

fs::path fresh_dir(const std::string& name) {
  fs::path dir = fs::temp_directory_path() / name;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

size_t entries_in(const fs::path& dir) {
  size_t n = 0;
  for ([[maybe_unused]] const auto& e : fs::directory_iterator(dir)) ++n;
  return n;
}

// Makes every write past `bytes` fail with EFBIG (SIGXFSZ ignored) for the
// scope -- a portable stand-in for a full disk.
class FileSizeLimit {
 public:
  explicit FileSizeLimit(rlim_t bytes) {
    getrlimit(RLIMIT_FSIZE, &old_);
    old_handler_ = ::signal(SIGXFSZ, SIG_IGN);
    rlimit lim = old_;
    lim.rlim_cur = bytes;
    setrlimit(RLIMIT_FSIZE, &lim);
  }
  ~FileSizeLimit() {
    setrlimit(RLIMIT_FSIZE, &old_);
    ::signal(SIGXFSZ, old_handler_);
  }

 private:
  rlimit old_{};
  void (*old_handler_)(int) = nullptr;
};

}  // namespace

TEST_CASE(":q refuses while another buffer has unsaved changes", "[quit][save-safety]") {
  fs::path dir = fresh_dir("zedit_test_q_dirty");
  write_file((dir / "a.txt").string(), "a\n");
  write_file((dir / "b.txt").string(), "b\n");
  Editor ed = Editor::open_file((dir / "a.txt").string());
  feed(ed, "ix\x1b");  // buffer 0 dirty
  ed.open_buffer((dir / "b.txt").string());
  REQUIRE_FALSE(ed.dirty());  // the current buffer is clean...
  feed(ed, ":q\n");
  REQUIRE_FALSE(ed.should_quit());  // ...but buffer 0 isn't
  REQUIRE(ed.last_error().find("E162") != std::string::npos);
  REQUIRE(ed.last_error().find("a.txt") != std::string::npos);
  feed(ed, ":q!\n");
  REQUIRE(ed.should_quit());
}

TEST_CASE(":wq saves the current buffer but won't quit over another dirty one",
          "[quit][save-safety]") {
  fs::path dir = fresh_dir("zedit_test_wq_dirty");
  write_file((dir / "a.txt").string(), "a\n");
  write_file((dir / "b.txt").string(), "b\n");
  Editor ed = Editor::open_file((dir / "a.txt").string());
  feed(ed, "ix\x1b");
  ed.open_buffer((dir / "b.txt").string());
  feed(ed, "iy\x1b:wq\n");
  REQUIRE(read_file((dir / "b.txt").string()) == "yb\n");
  REQUIRE_FALSE(ed.should_quit());
  REQUIRE(ed.last_error().find("a.txt") != std::string::npos);
}

TEST_CASE(":q and :wq still quit when every buffer is clean", "[quit][save-safety]") {
  fs::path dir = fresh_dir("zedit_test_q_clean");
  write_file((dir / "a.txt").string(), "a\n");
  {
    Editor ed = Editor::open_file((dir / "a.txt").string());
    feed(ed, ":q\n");
    REQUIRE(ed.should_quit());
  }
  {
    Editor ed = Editor::open_file((dir / "a.txt").string());
    feed(ed, "iz\x1b:wq\n");
    REQUIRE(ed.should_quit());
    REQUIRE(read_file((dir / "a.txt").string()) == "za\n");
  }
}

TEST_CASE("a write that fails part-way keeps the original and the buffer dirty",
          "[file_io][save-safety]") {
  fs::path dir = fresh_dir("zedit_test_save_efbig");
  std::string path = (dir / "f.txt").string();
  write_file(path, "original\n");
  Editor ed = Editor::open_file(path);
  feed(ed, "i");
  for (int i = 0; i < 200; ++i) feed(ed, "x");
  feed(ed, "\x1b");
  {
    FileSizeLimit limit(64);
    REQUIRE_THROWS_AS(ed.save(), FileIoError);
  }
  REQUIRE(ed.dirty());
  REQUIRE(read_file(path) == "original\n");
  REQUIRE(entries_in(dir) == 1);  // the temp file was cleaned up
  ed.save();                      // and a later save works
  REQUIRE_FALSE(ed.dirty());
}

TEST_CASE("saving to /dev/full reports the error", "[file_io][save-safety]") {
  if (!fs::exists("/dev/full")) {
    SKIP("no /dev/full on this platform");
  }
  Editor ed;
  ed.set_filename("/dev/full");
  feed(ed, "ihello\x1b");
  REQUIRE_THROWS_AS(ed.save(), FileIoError);
  REQUIRE(ed.dirty());
  REQUIRE(fs::is_character_file("/dev/full"));  // written in place, not replaced
}

TEST_CASE("a :w failure shows on the status line", "[file_io][save-safety]") {
  Editor ed;
  ed.set_filename("/nonexistent-zedit-dir/f.txt");
  feed(ed, "ihi\x1b:w\n");
  REQUIRE(ed.dirty());
  REQUIRE(ed.last_error().find("failed to open file for writing") != std::string::npos);
}

TEST_CASE("save keeps the file's permissions", "[file_io][save-safety]") {
  fs::path dir = fresh_dir("zedit_test_save_mode");
  std::string path = (dir / "s.sh").string();
  write_file(path, "#!/bin/sh\n");
  ::chmod(path.c_str(), 0751);
  write_file(path, "#!/bin/sh\necho hi\n");
  struct stat st {};
  REQUIRE(::stat(path.c_str(), &st) == 0);
  REQUIRE((st.st_mode & 07777) == 0751);
  REQUIRE(read_file(path) == "#!/bin/sh\necho hi\n");
  REQUIRE(entries_in(dir) == 1);
}

TEST_CASE("a new file gets the umask's default permissions", "[file_io][save-safety]") {
  fs::path dir = fresh_dir("zedit_test_save_newmode");
  std::string path = (dir / "n.txt").string();
  mode_t old = ::umask(022);
  write_file(path, "new\n");
  ::umask(old);
  struct stat st {};
  REQUIRE(::stat(path.c_str(), &st) == 0);
  REQUIRE((st.st_mode & 0777) == 0644);
}

TEST_CASE("save through a symlink updates the target and keeps the link",
          "[file_io][save-safety]") {
  fs::path dir = fresh_dir("zedit_test_save_symlink");
  fs::create_directories(dir / "real");
  std::string target = (dir / "real" / "t.txt").string();
  write_file(target, "old\n");
  fs::create_symlink(dir / "real" / "t.txt", dir / "link.txt");
  write_file((dir / "link.txt").string(), "new\n");
  REQUIRE(fs::is_symlink(dir / "link.txt"));
  REQUIRE(read_file(target) == "new\n");
  REQUIRE(entries_in(dir / "real") == 1);
}
