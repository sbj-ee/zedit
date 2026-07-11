#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "zedit/core/diff.hpp"

using zedit::core::diff_lines;
using zedit::core::DiffLineStatus;

TEST_CASE("identical sequences are entirely unchanged", "[diff]") {
  std::vector<std::string> a = {"one", "two", "three"};
  std::vector<std::string> b = {"one", "two", "three"};
  auto result = diff_lines(a, b);
  for (auto s : result.left) REQUIRE(s == DiffLineStatus::Unchanged);
  for (auto s : result.right) REQUIRE(s == DiffLineStatus::Unchanged);
}

TEST_CASE("a fully-added sequence marks every right line as Added", "[diff]") {
  std::vector<std::string> a = {};
  std::vector<std::string> b = {"x", "y"};
  auto result = diff_lines(a, b);
  REQUIRE(result.left.empty());
  REQUIRE(result.right == std::vector<DiffLineStatus>{DiffLineStatus::Added, DiffLineStatus::Added});
}

TEST_CASE("a fully-removed sequence marks every left line as Removed", "[diff]") {
  std::vector<std::string> a = {"x", "y"};
  std::vector<std::string> b = {};
  auto result = diff_lines(a, b);
  REQUIRE(result.right.empty());
  REQUIRE(result.left ==
          std::vector<DiffLineStatus>{DiffLineStatus::Removed, DiffLineStatus::Removed});
}

TEST_CASE("a single changed line in the middle is Removed on the left and Added on the right",
          "[diff]") {
  std::vector<std::string> a = {"a", "b", "c"};
  std::vector<std::string> b = {"a", "x", "c"};
  auto result = diff_lines(a, b);
  REQUIRE(result.left == std::vector<DiffLineStatus>{DiffLineStatus::Unchanged,
                                                       DiffLineStatus::Removed,
                                                       DiffLineStatus::Unchanged});
  REQUIRE(result.right == std::vector<DiffLineStatus>{DiffLineStatus::Unchanged,
                                                        DiffLineStatus::Added,
                                                        DiffLineStatus::Unchanged});
}

TEST_CASE("an inserted line shows only as Added, surrounding lines stay Unchanged", "[diff]") {
  std::vector<std::string> a = {"a", "b"};
  std::vector<std::string> b = {"a", "new", "b"};
  auto result = diff_lines(a, b);
  REQUIRE(result.left == std::vector<DiffLineStatus>{DiffLineStatus::Unchanged,
                                                       DiffLineStatus::Unchanged});
  REQUIRE(result.right == std::vector<DiffLineStatus>{
                              DiffLineStatus::Unchanged, DiffLineStatus::Added, DiffLineStatus::Unchanged});
}

TEST_CASE("both sequences empty produces empty results", "[diff]") {
  auto result = diff_lines({}, {});
  REQUIRE(result.left.empty());
  REQUIRE(result.right.empty());
}
