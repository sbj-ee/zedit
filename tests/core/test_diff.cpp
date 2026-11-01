#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <random>
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

namespace {

// Reference LCS length via the old O(N*M) table, to check Myers finds an
// optimal alignment.
size_t lcs_length(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  std::vector<std::vector<size_t>> dp(a.size() + 1, std::vector<size_t>(b.size() + 1, 0));
  for (size_t i = a.size(); i-- > 0;) {
    for (size_t j = b.size(); j-- > 0;) {
      dp[i][j] = a[i] == b[j] ? dp[i + 1][j + 1] + 1 : std::max(dp[i + 1][j], dp[i][j + 1]);
    }
  }
  return dp[0][0];
}

// Checks the result is a valid alignment (Unchanged lines pair up in order
// with equal text) and returns how many lines it keeps.
size_t checked_unchanged(const std::vector<std::string>& a, const std::vector<std::string>& b,
                         const zedit::core::DiffResult& r) {
  std::vector<std::string> kept_a;
  std::vector<std::string> kept_b;
  for (size_t i = 0; i < a.size(); ++i) {
    if (r.left[i] == DiffLineStatus::Unchanged) kept_a.push_back(a[i]);
    REQUIRE(r.left[i] != DiffLineStatus::Added);
  }
  for (size_t j = 0; j < b.size(); ++j) {
    if (r.right[j] == DiffLineStatus::Unchanged) kept_b.push_back(b[j]);
    REQUIRE(r.right[j] != DiffLineStatus::Removed);
  }
  REQUIRE(kept_a == kept_b);
  return kept_a.size();
}

}  // namespace

TEST_CASE("Myers diff matches the LCS optimum on random inputs", "[diff]") {
  std::mt19937 rng(12345);
  for (int iter = 0; iter < 500; ++iter) {
    std::uniform_int_distribution<int> len(0, 25);
    std::uniform_int_distribution<int> alpha(0, 3);
    std::vector<std::string> a(static_cast<size_t>(len(rng)));
    std::vector<std::string> b(static_cast<size_t>(len(rng)));
    for (auto& s : a) s = std::string(1, static_cast<char>('a' + alpha(rng)));
    for (auto& s : b) s = std::string(1, static_cast<char>('a' + alpha(rng)));
    auto r = diff_lines(a, b);
    REQUIRE(r.left.size() == a.size());
    REQUIRE(r.right.size() == b.size());
    REQUIRE(checked_unchanged(a, b, r) == lcs_length(a, b));
  }
}

TEST_CASE("diff of a large file with a few edits is fast and exact", "[diff]") {
  std::vector<std::string> a;
  for (int i = 0; i < 50000; ++i) a.push_back("line " + std::to_string(i));
  std::vector<std::string> b = a;
  b[10] = "changed";
  b.insert(b.begin() + 25000, "inserted");
  b.erase(b.begin() + 40000);

  auto start = std::chrono::steady_clock::now();
  auto r = diff_lines(a, b);
  auto elapsed = std::chrono::steady_clock::now() - start;
  // The old O(N*M) table would need 2.5e9 cells here.
  REQUIRE(elapsed < std::chrono::seconds(2));
  REQUIRE(checked_unchanged(a, b, r) == a.size() - 2);
  REQUIRE(r.right[10] == DiffLineStatus::Added);
  REQUIRE(r.right[25000] == DiffLineStatus::Added);
  REQUIRE(r.left[10] == DiffLineStatus::Removed);
}
