#include <catch2/catch_test_macros.hpp>

#include "zedit/core/version.hpp"

TEST_CASE("version string is non-empty", "[version]") {
  REQUIRE(std::string(zedit::core::version_string()).size() > 0);
}
