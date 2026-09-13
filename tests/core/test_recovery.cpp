#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

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
