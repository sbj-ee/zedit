#include <catch2/catch_test_macros.hpp>

#include <string>

#include "zedit/core/piece_table.hpp"
#include "zedit/core/search.hpp"

using zedit::core::PieceTable;
using zedit::core::search_backward;
using zedit::core::search_forward;

TEST_CASE("search_forward finds the next occurrence after the cursor", "[search]") {
  PieceTable buf(std::string("foo bar foo baz"));
  auto pos = search_forward(buf, 0, "foo");
  REQUIRE(pos.has_value());
  REQUIRE(*pos == 8);  // the second "foo", not the one under the cursor
}

TEST_CASE("search_forward wraps around to the start", "[search]") {
  PieceTable buf(std::string("foo bar foo baz"));
  auto pos = search_forward(buf, 8, "foo");
  REQUIRE(pos.has_value());
  REQUIRE(*pos == 0);
}

TEST_CASE("search_forward on the sole occurrence returns to itself", "[search]") {
  PieceTable buf(std::string("foo bar baz"));
  auto pos = search_forward(buf, 0, "foo");
  REQUIRE(pos.has_value());
  REQUIRE(*pos == 0);
}

TEST_CASE("search_forward returns nullopt when the pattern is absent", "[search]") {
  PieceTable buf(std::string("foo bar baz"));
  REQUIRE_FALSE(search_forward(buf, 0, "xyz").has_value());
}

TEST_CASE("search_backward finds the previous occurrence before the cursor", "[search]") {
  PieceTable buf(std::string("foo bar foo baz"));
  auto pos = search_backward(buf, 8, "foo");
  REQUIRE(pos.has_value());
  REQUIRE(*pos == 0);
}

TEST_CASE("search_backward wraps around to the end", "[search]") {
  PieceTable buf(std::string("foo bar foo baz"));
  auto pos = search_backward(buf, 0, "foo");
  REQUIRE(pos.has_value());
  REQUIRE(*pos == 8);
}

TEST_CASE("search with an empty pattern or empty buffer finds nothing", "[search]") {
  PieceTable buf(std::string("hello"));
  REQUIRE_FALSE(search_forward(buf, 0, "").has_value());
  PieceTable empty;
  REQUIRE_FALSE(search_forward(empty, 0, "x").has_value());
}
