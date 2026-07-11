#include <catch2/catch_test_macros.hpp>

#include <string>

#include "zedit/core/motion.hpp"
#include "zedit/core/piece_table.hpp"

using zedit::core::motion_line_end;
using zedit::core::motion_line_start;
using zedit::core::motion_word_backward;
using zedit::core::motion_word_end_forward;
using zedit::core::motion_word_forward;
using zedit::core::PieceTable;
using zedit::core::Range;
using zedit::core::text_object_inner_word;

TEST_CASE("word forward skips the current word and following spaces", "[motion]") {
  PieceTable buf(std::string("foo bar  baz"));
  REQUIRE(motion_word_forward(buf, 0) == 4);   // "foo " -> "bar"
  REQUIRE(motion_word_forward(buf, 4) == 9);   // "bar  " -> "baz"
  REQUIRE(motion_word_forward(buf, 9) == 12);  // "baz" -> end of buffer
}

TEST_CASE("word forward treats punctuation as its own word", "[motion]") {
  PieceTable buf(std::string("foo::bar"));
  REQUIRE(motion_word_forward(buf, 0) == 3);  // "foo" -> "::"
  REQUIRE(motion_word_forward(buf, 3) == 5);  // "::" -> "bar"
}

TEST_CASE("word backward moves to the start of the previous word", "[motion]") {
  PieceTable buf(std::string("foo bar  baz"));
  REQUIRE(motion_word_backward(buf, 12) == 9);  // end -> "baz"
  REQUIRE(motion_word_backward(buf, 9) == 4);   // "baz" -> "bar"
  REQUIRE(motion_word_backward(buf, 4) == 0);   // "bar" -> "foo"
  REQUIRE(motion_word_backward(buf, 0) == 0);   // already at start
}

TEST_CASE("word end forward moves to the end of the current or next word", "[motion]") {
  PieceTable buf(std::string("foo bar baz"));
  REQUIRE(motion_word_end_forward(buf, 0) == 2);  // 'f' -> end of "foo"
  REQUIRE(motion_word_end_forward(buf, 2) == 6);  // end of "foo" -> end of "bar"
  REQUIRE(motion_word_end_forward(buf, 6) == 10); // end of "bar" -> end of "baz"
}

TEST_CASE("line start and end motions", "[motion]") {
  PieceTable buf(std::string("  hello\nworld"));
  REQUIRE(motion_line_start(buf, 4) == 0);
  REQUIRE(motion_line_end(buf, 0) == 6);   // last char of "  hello" is 'o' at index 6
  REQUIRE(motion_line_end(buf, 9) == 12);  // last char of "world"
}

TEST_CASE("line end on an empty line returns the line start", "[motion]") {
  PieceTable buf(std::string("a\n\nb"));
  REQUIRE(motion_line_end(buf, 2) == 2);
}

TEST_CASE("inner word text object selects the word under the cursor", "[motion]") {
  PieceTable buf(std::string("foo bar baz"));
  Range r = text_object_inner_word(buf, 5);  // inside "bar"
  REQUIRE(r.start == 4);
  REQUIRE(r.end == 7);
}

TEST_CASE("inner word text object selects whitespace runs and punctuation runs", "[motion]") {
  PieceTable buf(std::string("foo   bar"));
  Range space_range = text_object_inner_word(buf, 4);
  REQUIRE(space_range.start == 3);
  REQUIRE(space_range.end == 6);

  PieceTable buf2(std::string("foo::bar"));
  Range punct_range = text_object_inner_word(buf2, 3);
  REQUIRE(punct_range.start == 3);
  REQUIRE(punct_range.end == 5);
}
