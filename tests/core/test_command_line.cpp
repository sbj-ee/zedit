#include <catch2/catch_test_macros.hpp>

#include "zedit/core/command_line.hpp"

using zedit::core::ExCommandKind;
using zedit::core::parse_ex_command;

TEST_CASE("parse_ex_command recognizes known commands", "[command_line]") {
  REQUIRE(parse_ex_command("").kind == ExCommandKind::Empty);
  REQUIRE(parse_ex_command("  ").kind == ExCommandKind::Empty);
  REQUIRE(parse_ex_command("w").kind == ExCommandKind::Write);
  REQUIRE(parse_ex_command("q").kind == ExCommandKind::Quit);
  REQUIRE(parse_ex_command("q!").kind == ExCommandKind::ForceQuit);
  REQUIRE(parse_ex_command("wq").kind == ExCommandKind::WriteQuit);
  REQUIRE(parse_ex_command("x").kind == ExCommandKind::WriteQuit);
  REQUIRE(parse_ex_command("  w  ").kind == ExCommandKind::Write);
}

TEST_CASE("parse_ex_command rejects unknown commands", "[command_line]") {
  REQUIRE(parse_ex_command("bogus").kind == ExCommandKind::Unknown);
  REQUIRE(parse_ex_command("wqq").kind == ExCommandKind::Unknown);
}
