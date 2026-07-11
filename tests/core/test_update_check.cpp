#include <catch2/catch_test_macros.hpp>

#include "zedit/core/update_check.hpp"

using zedit::core::parse_newer_version;

TEST_CASE("parse_newer_version detects a newer patch release", "[update-check]") {
  auto info = parse_newer_version(
      "0.1.0", R"({"tag_name": "v0.1.1", "html_url": "https://example.com/v0.1.1"})");
  REQUIRE(info.has_value());
  REQUIRE(info->version == "v0.1.1");
  REQUIRE(info->url == "https://example.com/v0.1.1");
}

TEST_CASE("parse_newer_version detects a newer minor/major release", "[update-check]") {
  REQUIRE(parse_newer_version("0.1.0", R"({"tag_name": "v0.2.0"})").has_value());
  REQUIRE(parse_newer_version("0.1.0", R"({"tag_name": "v1.0.0"})").has_value());
}

TEST_CASE("parse_newer_version returns nullopt when already current", "[update-check]") {
  REQUIRE_FALSE(parse_newer_version("0.1.0", R"({"tag_name": "v0.1.0"})").has_value());
}

TEST_CASE("parse_newer_version returns nullopt when the release is older", "[update-check]") {
  REQUIRE_FALSE(parse_newer_version("0.5.0", R"({"tag_name": "v0.1.0"})").has_value());
}

TEST_CASE("parse_newer_version tolerates a missing leading 'v' on either side",
          "[update-check]") {
  REQUIRE(parse_newer_version("v0.1.0", R"({"tag_name": "0.2.0"})").has_value());
}

TEST_CASE("parse_newer_version treats missing trailing components as 0", "[update-check]") {
  // "0.2" beats "0.1.9" -- the missing patch component reads as 0, but
  // minor (2 vs 1) already decides it.
  REQUIRE(parse_newer_version("0.1.9", R"({"tag_name": "v0.2"})").has_value());
}

TEST_CASE("parse_newer_version returns nullopt for malformed JSON", "[update-check]") {
  REQUIRE_FALSE(parse_newer_version("0.1.0", "not json at all").has_value());
}

TEST_CASE("parse_newer_version returns nullopt when tag_name is missing", "[update-check]") {
  // GitHub's actual response when no releases have been published yet is
  // a 404 body like {"message": "Not Found", ...} -- no tag_name.
  REQUIRE_FALSE(parse_newer_version("0.1.0", R"({"message": "Not Found"})").has_value());
}

TEST_CASE("parse_newer_version returns nullopt for an empty string", "[update-check]") {
  REQUIRE_FALSE(parse_newer_version("0.1.0", "").has_value());
}
