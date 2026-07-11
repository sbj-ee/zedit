#include <catch2/catch_test_macros.hpp>

#include "zedit/core/word_wrap.hpp"

using zedit::core::wrap_line;
using zedit::core::WrapSegment;

TEST_CASE("wrap_line returns one segment when the line already fits", "[word-wrap]") {
  auto segs = wrap_line("hello", 10);
  REQUIRE(segs.size() == 1);
  REQUIRE(segs[0].start == 0);
  REQUIRE(segs[0].end == 5);
}

TEST_CASE("wrap_line returns one segment for a line exactly max_chars long", "[word-wrap]") {
  auto segs = wrap_line("0123456789", 10);
  REQUIRE(segs.size() == 1);
  REQUIRE(segs[0].end == 10);
}

TEST_CASE("wrap_line returns one empty segment for an empty line", "[word-wrap]") {
  auto segs = wrap_line("", 10);
  REQUIRE(segs.size() == 1);
  REQUIRE(segs[0].start == 0);
  REQUIRE(segs[0].end == 0);
}

TEST_CASE("wrap_line breaks at the last space before the limit", "[word-wrap]") {
  // "the quick brown fox" with max_chars=10:
  //   "the quick " is 10 chars -- break before "quick"'s trailing space,
  //   i.e. segment 0 is "the quick" (9 chars, space consumed).
  auto segs = wrap_line("the quick brown fox", 10);
  REQUIRE(segs.size() == 2);
  REQUIRE(segs[0].start == 0);
  REQUIRE(segs[0].end == 9);  // "the quick", space at index 9 consumed
  REQUIRE(segs[1].start == 10);
  REQUIRE(segs[1].end == 19);  // "brown fox"
}

TEST_CASE("wrap_line hard-breaks a run with no space", "[word-wrap]") {
  auto segs = wrap_line("aaaaaaaaaaaaaaaaaaaa", 8);  // 20 a's, no spaces
  REQUIRE(segs.size() == 3);
  REQUIRE(segs[0].end - segs[0].start == 8);
  REQUIRE(segs[1].end - segs[1].start == 8);
  REQUIRE(segs[2].end - segs[2].start == 4);
}

TEST_CASE("wrap_line with max_chars 0 returns the whole line unsplit", "[word-wrap]") {
  auto segs = wrap_line("hello world", 0);
  REQUIRE(segs.size() == 1);
  REQUIRE(segs[0].end == 11);
}

TEST_CASE("wrap_line segments reconstruct the original line (minus consumed spaces)",
          "[word-wrap]") {
  std::string_view line = "one two three four five six seven";
  auto segs = wrap_line(line, 12);
  std::string rebuilt;
  for (size_t i = 0; i < segs.size(); ++i) {
    rebuilt += std::string(line.substr(segs[i].start, segs[i].end - segs[i].start));
    if (i + 1 < segs.size()) {
      rebuilt += ' ';
    }
  }
  REQUIRE(rebuilt == line);
}
