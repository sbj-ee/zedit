#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <sys/stat.h>
#include <unistd.h>

#include "zedit/core/recovery.hpp"

using zedit::core::Baseline;
using zedit::core::RecoveryDebouncer;
using zedit::core::clear_swap;
using zedit::core::default_swap_dir;
using zedit::core::read_swap;
using zedit::core::swap_path_for;
using zedit::core::write_swap_atomic;

namespace {

struct SwapDirGuard {
  std::filesystem::path dir;
  SwapDirGuard() {
    dir = std::filesystem::temp_directory_path() /
          ("zedit_recovery_test_" + std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    setenv("ZEDIT_SWAP_DIR", dir.c_str(), 1);
  }
  ~SwapDirGuard() {
    unsetenv("ZEDIT_SWAP_DIR");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
};

}  // namespace

TEST_CASE("default_swap_dir honors ZEDIT_SWAP_DIR", "[recovery]") {
  SwapDirGuard guard;
  REQUIRE(default_swap_dir() == guard.dir.string());
}

TEST_CASE("write_swap_atomic / read_swap roundtrip", "[recovery]") {
  SwapDirGuard guard;
  const std::string abs = "/tmp/zedit_recovery_roundtrip_doc.txt";
  Baseline baseline;
  baseline.mtime_ns = 1234567890;
  baseline.size = 42;
  baseline.inode = 99;
  const std::string content = "hello\nunsaved\nworld";

  REQUIRE(write_swap_atomic(abs, content, baseline));
  auto payload = read_swap(abs);
  REQUIRE(payload.has_value());
  REQUIRE(payload->path == abs);
  REQUIRE(payload->content == content);
  REQUIRE(payload->baseline.mtime_ns == baseline.mtime_ns);
  REQUIRE(payload->baseline.size == baseline.size);
  REQUIRE(payload->baseline.inode == baseline.inode);
  REQUIRE(payload->saved_at_ns > 0);
  REQUIRE(std::filesystem::exists(swap_path_for(abs)));
}

TEST_CASE("clear_swap removes the file", "[recovery]") {
  SwapDirGuard guard;
  const std::string abs = "/tmp/zedit_recovery_clear_doc.txt";
  REQUIRE(write_swap_atomic(abs, "x", Baseline{}));
  REQUIRE(std::filesystem::exists(swap_path_for(abs)));
  clear_swap(abs);
  REQUIRE_FALSE(std::filesystem::exists(swap_path_for(abs)));
  REQUIRE_FALSE(read_swap(abs).has_value());
}

TEST_CASE("corrupt swap is ignored and deleted", "[recovery]") {
  SwapDirGuard guard;
  const std::string abs = "/tmp/zedit_recovery_corrupt_doc.txt";
  const auto swp = swap_path_for(abs);
  std::filesystem::create_directories(std::filesystem::path(swp).parent_path());
  {
    std::ofstream out(swp, std::ios::binary);
    out << "NOTASWAPFILE!!!!";
  }
  REQUIRE(std::filesystem::exists(swp));
  REQUIRE_FALSE(read_swap(abs).has_value());
  REQUIRE_FALSE(std::filesystem::exists(swp));
}

TEST_CASE("truncated swap is ignored and deleted", "[recovery]") {
  SwapDirGuard guard;
  const std::string abs = "/tmp/zedit_recovery_trunc_doc.txt";
  // Write a valid swap then truncate it.
  REQUIRE(write_swap_atomic(abs, "full content here", Baseline{}));
  const auto swp = swap_path_for(abs);
  {
    std::ofstream out(swp, std::ios::binary | std::ios::trunc);
    out.write("ZEDITSWP", 8);
    // version + truncated junk
    char junk[6] = {};
    out.write(junk, 6);
  }
  REQUIRE_FALSE(read_swap(abs).has_value());
  REQUIRE_FALSE(std::filesystem::exists(swp));
}

TEST_CASE("RecoveryDebouncer fires after delay", "[recovery]") {
  RecoveryDebouncer d;
  REQUIRE_FALSE(d.poll());
  d.arm(std::chrono::milliseconds(30));
  REQUIRE(d.armed());
  REQUIRE_FALSE(d.poll());
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  REQUIRE(d.poll());
  REQUIRE_FALSE(d.armed());
  REQUIRE_FALSE(d.poll());
}

TEST_CASE("consider_recovery offers differing content and skips identical", "[recovery]") {
  SwapDirGuard guard;
  const std::string abs = "/tmp/zedit_recovery_offer_doc.txt";
  REQUIRE(write_swap_atomic(abs, "unsaved", Baseline{}));
  auto offer = zedit::core::consider_recovery(abs, "on disk");
  REQUIRE(offer.has_value());
  REQUIRE(offer->content == "unsaved");

  // Identical to disk -> clear, no offer
  REQUIRE(write_swap_atomic(abs, "same", Baseline{}));
  REQUIRE_FALSE(zedit::core::consider_recovery(abs, "same").has_value());
  REQUIRE_FALSE(std::filesystem::exists(swap_path_for(abs)));
}

TEST_CASE("swap file names are unchanged by the shared SHA-256", "[recovery][swap-hardening]") {
  // Same name earlier versions used, so their swaps are still found.
  setenv("ZEDIT_SWAP_DIR", "/swapdir", 1);
  REQUIRE(swap_path_for("/home/someone/notes.txt") ==
          "/swapdir/08e7f195193050fb7373a8d517a893270de9259062a2154d6eae7341087c44d8.swp");
  unsetenv("ZEDIT_SWAP_DIR");
}

TEST_CASE("swap files are 0600 in a 0700 directory", "[recovery][swap-hardening]") {
  SwapDirGuard guard;
  std::filesystem::path dir = guard.dir / "swap";
  setenv("ZEDIT_SWAP_DIR", dir.c_str(), 1);
  const std::string abs = "/tmp/zedit_recovery_mode_doc.txt";
  REQUIRE(write_swap_atomic(abs, "secret", Baseline{}));
  struct stat st {};
  REQUIRE(::stat(swap_path_for(abs).c_str(), &st) == 0);
  REQUIRE((st.st_mode & 0777) == 0600);
  REQUIRE(::stat(dir.c_str(), &st) == 0);
  REQUIRE((st.st_mode & 0777) == 0700);
}

TEST_CASE("an existing world-readable swap directory is tightened to 0700",
          "[recovery][swap-hardening]") {
  SwapDirGuard guard;
  std::filesystem::path dir = guard.dir / "swap";
  std::filesystem::create_directories(dir);
  ::chmod(dir.c_str(), 0755);
  setenv("ZEDIT_SWAP_DIR", dir.c_str(), 1);
  REQUIRE(write_swap_atomic("/tmp/zedit_recovery_tighten.txt", "x", Baseline{}));
  struct stat st {};
  REQUIRE(::stat(dir.c_str(), &st) == 0);
  REQUIRE((st.st_mode & 0777) == 0700);
}

TEST_CASE("a symlinked swap directory is refused", "[recovery][swap-hardening]") {
  SwapDirGuard guard;
  std::filesystem::path real = guard.dir / "elsewhere";
  std::filesystem::create_directories(real);
  std::filesystem::path link = guard.dir / "swap";
  std::filesystem::create_directory_symlink(real, link);
  setenv("ZEDIT_SWAP_DIR", link.c_str(), 1);
  REQUIRE_FALSE(write_swap_atomic("/tmp/zedit_recovery_symlink.txt", "x", Baseline{}));
  REQUIRE(std::filesystem::is_empty(real));
}

TEST_CASE("a swap from a different format version is kept, not deleted",
          "[recovery][swap-hardening]") {
  SwapDirGuard guard;
  const std::string abs = "/tmp/zedit_recovery_future_doc.txt";
  REQUIRE(write_swap_atomic(abs, "from the future", Baseline{}));
  const auto swp = swap_path_for(abs);
  {
    std::fstream f(swp, std::ios::binary | std::ios::in | std::ios::out);
    f.seekp(8);
    const char v2[4] = {2, 0, 0, 0};  // version 2, little-endian
    f.write(v2, 4);
  }
  REQUIRE_FALSE(read_swap(abs).has_value());
  REQUIRE(std::filesystem::exists(swp));
}

TEST_CASE("with no HOME the swap fallback is per-user, not /tmp/zedit/swap",
          "[recovery][swap-hardening]") {
#if defined(__APPLE__)
  SKIP("macOS always has HOME here");
#else
  std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
  std::string xdg = std::getenv("XDG_CACHE_HOME") ? std::getenv("XDG_CACHE_HOME") : "";
  unsetenv("HOME");
  unsetenv("XDG_CACHE_HOME");
  unsetenv("ZEDIT_SWAP_DIR");
  std::string dir = default_swap_dir();
  if (!home.empty()) setenv("HOME", home.c_str(), 1);
  if (!xdg.empty()) setenv("XDG_CACHE_HOME", xdg.c_str(), 1);
  REQUIRE(dir == "/tmp/zedit-" + std::to_string(::geteuid()) + "/swap");
#endif
}
