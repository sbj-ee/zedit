#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>
#include <unistd.h>

namespace zedit::test {

// True if an executable `clangd` is on PATH.
inline bool clangd_on_path() {
  const char* path = std::getenv("PATH");
  if (path == nullptr) return false;
  std::string dirs(path);
  size_t start = 0;
  while (start <= dirs.size()) {
    size_t end = dirs.find(':', start);
    if (end == std::string::npos) end = dirs.size();
    std::string dir = dirs.substr(start, end - start);
    if (dir.empty()) dir = ".";
    if (::access((dir + "/clangd").c_str(), X_OK) == 0) return true;
    start = end + 1;
  }
  return false;
}

}  // namespace zedit::test

// Skips a test that needs a real clangd when there is none, so a build on a
// machine without clangd isn't a wall of red. CI installs clangd and sets
// ZEDIT_REQUIRE_CLANGD=1, which turns the skip back into a failure.
#define ZEDIT_REQUIRE_CLANGD_OR_SKIP()                                      \
  do {                                                                      \
    if (!zedit::test::clangd_on_path()) {                                   \
      if (std::getenv("ZEDIT_REQUIRE_CLANGD") != nullptr) {                 \
        FAIL("clangd not on PATH but ZEDIT_REQUIRE_CLANGD is set");         \
      }                                                                     \
      SKIP("clangd not on PATH");                                           \
    }                                                                       \
  } while (false)
