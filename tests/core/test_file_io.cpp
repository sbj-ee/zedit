#include <catch2/catch_test_macros.hpp>

#include <string>

#include "zedit/core/file_io.hpp"

using zedit::core::check_file_size_or_throw;
using zedit::core::FileIoError;
using zedit::core::FileTooLargeError;
using zedit::core::kMaxFileSizeBytes;
using zedit::core::read_file;
using zedit::core::write_file;

// Boundary logic only -- deliberately does NOT create a real 256MB+ file
// on disk to exercise read_file()'s end-to-end enforcement; that would be
// slow and wasteful in CI for no extra confidence beyond what these
// pure-function checks already give.
TEST_CASE("check_file_size_or_throw allows exactly the limit", "[file_io]") {
  REQUIRE_NOTHROW(check_file_size_or_throw(kMaxFileSizeBytes, "somefile.txt"));
}

TEST_CASE("check_file_size_or_throw rejects one byte over the limit", "[file_io]") {
  REQUIRE_THROWS_AS(check_file_size_or_throw(kMaxFileSizeBytes + 1, "somefile.txt"),
                     FileTooLargeError);
}

TEST_CASE("FileTooLargeError is catchable as FileIoError", "[file_io]") {
  try {
    check_file_size_or_throw(kMaxFileSizeBytes + 1, "somefile.txt");
    FAIL("expected an exception");
  } catch (const FileIoError& e) {
    REQUIRE(std::string(e.what()).find("too large") != std::string::npos);
  }
}

TEST_CASE("read_file still works normally for an ordinary small file", "[file_io]") {
  std::string path = "/tmp/zedit_test_file_io_small.txt";
  write_file(path, "hello world");
  REQUIRE(read_file(path) == "hello world");
}
