#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>

#include "zedit/core/piece_table.hpp"

using zedit::core::PieceTable;

TEST_CASE("empty piece table", "[piece_table]") {
  PieceTable pt;
  REQUIRE(pt.size() == 0);
  REQUIRE(pt.to_string().empty());
  REQUIRE(pt.line_count() == 1);
}

TEST_CASE("construct from existing content", "[piece_table]") {
  PieceTable pt(std::string("hello world"));
  REQUIRE(pt.size() == 11);
  REQUIRE(pt.to_string() == "hello world");
}

TEST_CASE("insert at start, middle, end", "[piece_table]") {
  PieceTable pt(std::string("hello world"));

  pt.insert(0, "[");
  REQUIRE(pt.to_string() == "[hello world");

  pt.insert(pt.size(), "]");
  REQUIRE(pt.to_string() == "[hello world]");

  pt.insert(6, "-");
  REQUIRE(pt.to_string() == "[hello- world]");
}

TEST_CASE("insert into empty buffer", "[piece_table]") {
  PieceTable pt;
  pt.insert(0, "abc");
  REQUIRE(pt.to_string() == "abc");
  pt.insert(3, "def");
  REQUIRE(pt.to_string() == "abcdef");
  pt.insert(3, "-");
  REQUIRE(pt.to_string() == "abc-def");
}

TEST_CASE("erase within a single piece", "[piece_table]") {
  PieceTable pt(std::string("hello world"));
  pt.erase(5, 6);
  REQUIRE(pt.to_string() == "hello");
}

TEST_CASE("erase spanning multiple pieces", "[piece_table]") {
  PieceTable pt(std::string("hello world"));
  pt.insert(5, ",");
  pt.insert(0, ">> ");
  REQUIRE(pt.to_string() == ">> hello, world");

  pt.erase(2, 9);
  REQUIRE(pt.to_string() == ">>orld");
}

TEST_CASE("erase clamps to buffer bounds", "[piece_table]") {
  PieceTable pt(std::string("abc"));
  pt.erase(1, 100);
  REQUIRE(pt.to_string() == "a");

  PieceTable pt2(std::string("abc"));
  pt2.erase(10, 5);
  REQUIRE(pt2.to_string() == "abc");
}

TEST_CASE("text_range extracts arbitrary spans", "[piece_table]") {
  PieceTable pt(std::string("0123456789"));
  pt.insert(5, "XYZ");
  REQUIRE(pt.to_string() == "01234XYZ56789");
  REQUIRE(pt.text_range(3, 4) == "34XY");
  REQUIRE(pt.text_range(0, 0).empty());
}

TEST_CASE("line index basics", "[piece_table]") {
  PieceTable pt(std::string("line0\nline1\nline2"));
  REQUIRE(pt.line_count() == 3);
  REQUIRE(pt.line_text(0) == "line0");
  REQUIRE(pt.line_text(1) == "line1");
  REQUIRE(pt.line_text(2) == "line2");

  REQUIRE(pt.line_start_offset(0) == 0);
  REQUIRE(pt.line_start_offset(1) == 6);
  REQUIRE(pt.line_start_offset(2) == 12);

  REQUIRE(pt.offset_to_line(0) == 0);
  REQUIRE(pt.offset_to_line(5) == 0);
  REQUIRE(pt.offset_to_line(6) == 1);
  REQUIRE(pt.offset_to_line(16) == 2);

  auto lc = pt.offset_to_line_col(8);
  REQUIRE(lc.line == 1);
  REQUIRE(lc.col == 2);

  REQUIRE(pt.line_col_to_offset(1, 2) == 8);
}

TEST_CASE("line index updates after edits", "[piece_table]") {
  PieceTable pt(std::string("abc\ndef"));
  REQUIRE(pt.line_count() == 2);

  pt.insert(1, "\n");
  REQUIRE(pt.line_count() == 3);
  REQUIRE(pt.line_text(0) == "a");
  REQUIRE(pt.line_text(1) == "bc");
  REQUIRE(pt.line_text(2) == "def");

  pt.erase(1, 1);
  REQUIRE(pt.line_count() == 2);
  REQUIRE(pt.to_string() == "abc\ndef");
}

TEST_CASE("trailing newline produces a trailing empty line", "[piece_table]") {
  PieceTable pt(std::string("abc\n"));
  REQUIRE(pt.line_count() == 2);
  REQUIRE(pt.line_text(0) == "abc");
  REQUIRE(pt.line_text(1).empty());
}

TEST_CASE("randomized fuzz vs std::string reference model", "[piece_table]") {
  std::mt19937 rng(12345);
  std::uniform_int_distribution<int> op_dist(0, 1);
  std::uniform_int_distribution<int> char_dist('a', 'z');

  PieceTable pt;
  std::string reference;

  for (int iter = 0; iter < 2000; ++iter) {
    if (reference.empty() || op_dist(rng) == 0) {
      std::uniform_int_distribution<size_t> pos_dist(0, reference.size());
      size_t pos = pos_dist(rng);
      std::uniform_int_distribution<size_t> len_dist(1, 8);
      size_t len = len_dist(rng);
      std::string text;
      text.reserve(len);
      for (size_t i = 0; i < len; ++i) {
        text.push_back(static_cast<char>(char_dist(rng)));
      }
      pt.insert(pos, text);
      reference.insert(pos, text);
    } else {
      std::uniform_int_distribution<size_t> pos_dist(0, reference.size() - 1);
      size_t pos = pos_dist(rng);
      std::uniform_int_distribution<size_t> len_dist(
          1, std::min<size_t>(8, reference.size() - pos));
      size_t len = len_dist(rng);
      pt.erase(pos, len);
      reference.erase(pos, len);
    }
    REQUIRE(pt.to_string() == reference);
    REQUIRE(pt.size() == reference.size());
  }
}
