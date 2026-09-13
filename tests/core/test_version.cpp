#include <catch2/catch_test_macros.hpp>

#include <string>

#include "zedit/core/version.hpp"

TEST_CASE("version string is non-empty semver-shaped", "[version]") {
  const std::string v = zedit::core::version_string();
  REQUIRE(v.size() > 0);
  REQUIRE(v == zedit::core::kVersionString);
  REQUIRE(v.find('.') != std::string::npos);
}

TEST_CASE("version components match version string", "[version]") {
  const std::string expected = std::to_string(zedit::core::kVersionMajor) + "." +
                               std::to_string(zedit::core::kVersionMinor) + "." +
                               std::to_string(zedit::core::kVersionPatch);
  REQUIRE(std::string(zedit::core::kVersionString) == expected);
  REQUIRE(std::string(zedit::core::version_string()) == expected);
}
