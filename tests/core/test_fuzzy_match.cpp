#include <catch2/catch_test_macros.hpp>

#include "zedit/core/fuzzy_match.hpp"

using zedit::core::fuzzy_filter;
using zedit::core::fuzzy_score;

TEST_CASE("fuzzy_score matches a subsequence in order", "[fuzzy-match]") {
  // m(4) s(9) t(10) h(18) -- non-contiguous but strictly increasing.
  REQUIRE(fuzzy_score("msth", "src/mode_state_machine.hpp").has_value());
}

TEST_CASE("fuzzy_score fails when characters are out of order", "[fuzzy-match]") {
  // 's' only occurs at indices 0 and 9, both before every 't' (10, 12) --
  // there's no way to match t-then-s-then-m in increasing order.
  REQUIRE_FALSE(fuzzy_score("tsm", "src/mode_state_machine.hpp").has_value());
}

TEST_CASE("fuzzy_score fails when a character is missing entirely", "[fuzzy-match]") {
  REQUIRE_FALSE(fuzzy_score("xyz", "editor.hpp").has_value());
}

TEST_CASE("fuzzy_score is case-insensitive", "[fuzzy-match]") {
  REQUIRE(fuzzy_score("EDITOR", "editor.hpp").has_value());
  REQUIRE(fuzzy_score("editor", "Editor.hpp").has_value());
}

TEST_CASE("fuzzy_score returns 0 for an empty query", "[fuzzy-match]") {
  REQUIRE(fuzzy_score("", "anything.cpp") == 0);
}

TEST_CASE("fuzzy_score prefers consecutive matches over scattered ones", "[fuzzy-match]") {
  auto tight = fuzzy_score("ed", "editor.hpp");         // consecutive, no separator involved
  auto scattered = fuzzy_score("ed", "eventide.hpp");   // same two letters, spread apart
  REQUIRE(tight.has_value());
  REQUIRE(scattered.has_value());
  REQUIRE(*tight > *scattered);
}

TEST_CASE("fuzzy_score prefers a match at the start of the string", "[fuzzy-match]") {
  auto at_start = fuzzy_score("ed", "editor.hpp");
  auto mid_word = fuzzy_score("ed", "unedited.hpp");
  REQUIRE(at_start.has_value());
  REQUIRE(mid_word.has_value());
  REQUIRE(*at_start > *mid_word);
}

TEST_CASE("fuzzy_score prefers a match right after a path separator", "[fuzzy-match]") {
  auto after_sep = fuzzy_score("ed", "core/editor.hpp");
  auto mid_word = fuzzy_score("ed", "unedited.hpp");
  REQUIRE(after_sep.has_value());
  REQUIRE(mid_word.has_value());
  REQUIRE(*after_sep > *mid_word);
}

TEST_CASE("fuzzy_filter keeps only matches, sorted best first", "[fuzzy-match]") {
  std::vector<std::string> candidates = {
      "frontend_imgui/src/editor_view.cpp",
      "core/src/editor.cpp",
      "core/include/zedit/core/editor.hpp",
      "README.md",
  };
  std::vector<std::string> result = fuzzy_filter("editor.hpp", candidates);
  REQUIRE(result.size() == 1);
  REQUIRE(result[0] == "core/include/zedit/core/editor.hpp");
}

TEST_CASE("fuzzy_filter with an empty query returns everything in original order",
          "[fuzzy-match]") {
  std::vector<std::string> candidates = {"a.txt", "b.txt", "c.txt"};
  REQUIRE(fuzzy_filter("", candidates) == candidates);
}

TEST_CASE("fuzzy_filter respects max_results", "[fuzzy-match]") {
  std::vector<std::string> candidates = {"a1.txt", "a2.txt", "a3.txt", "a4.txt"};
  REQUIRE(fuzzy_filter("a", candidates, /*max_results=*/2).size() == 2);
}
