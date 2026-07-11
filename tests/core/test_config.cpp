#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>

#include "zedit/core/config.hpp"
#include "zedit/core/file_io.hpp"

using zedit::core::default_config_path;
using zedit::core::eval_lua;
using zedit::core::load_config_file;

TEST_CASE("eval_lua sets an option via zedit.set_option", "[config]") {
  auto result = eval_lua(R"(zedit.set_option("tabstop", 2))");
  REQUIRE(result.errors.empty());
  REQUIRE(result.options.tabstop.has_value());
  REQUIRE(*result.options.tabstop == 2);
}

TEST_CASE("eval_lua records an error for an unknown option name", "[config]") {
  auto result = eval_lua(R"(zedit.set_option("bogus", 1))");
  REQUIRE_FALSE(result.errors.empty());
  REQUIRE(result.options.tabstop == std::nullopt);
}

TEST_CASE("eval_lua parses a hex color via zedit.set_color", "[config]") {
  auto result = eval_lua(R"(zedit.set_color("comment", "#89b4fa"))");
  REQUIRE(result.errors.empty());
  REQUIRE(result.colors.count("comment") == 1);
  REQUIRE(result.colors.at("comment").r == 0x89);
  REQUIRE(result.colors.at("comment").g == 0xb4);
  REQUIRE(result.colors.at("comment").b == 0xfa);
}

TEST_CASE("eval_lua accepts a hex color without the leading #", "[config]") {
  auto result = eval_lua(R"(zedit.set_color("comment", "ff0000"))");
  REQUIRE(result.errors.empty());
  REQUIRE(result.colors.at("comment").r == 0xff);
}

TEST_CASE("eval_lua records an error for an invalid hex color", "[config]") {
  auto result = eval_lua(R"(zedit.set_color("comment", "not-a-color"))");
  REQUIRE_FALSE(result.errors.empty());
  REQUIRE(result.colors.empty());
}

TEST_CASE("eval_lua records a keymap via zedit.map", "[config]") {
  auto result = eval_lua(R"(zedit.map("n", "x", "dd"))");
  REQUIRE(result.errors.empty());
  REQUIRE(result.normal_remap.count('x') == 1);
  REQUIRE(result.normal_remap.at('x') == "dd");
}

TEST_CASE("eval_lua rejects a multi-character lhs for zedit.map", "[config]") {
  auto result = eval_lua(R"(zedit.map("n", "gx", "dd"))");
  REQUIRE_FALSE(result.errors.empty());
  REQUIRE(result.normal_remap.empty());
}

TEST_CASE("eval_lua rejects an unsupported mode for zedit.map", "[config]") {
  auto result = eval_lua(R"(zedit.map("i", "x", "dd"))");
  REQUIRE_FALSE(result.errors.empty());
  REQUIRE(result.normal_remap.empty());
}

TEST_CASE("eval_lua surfaces a Lua syntax error without crashing", "[config]") {
  auto result = eval_lua("this is not lua(((");
  REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("eval_lua keeps earlier effects when a later statement errors", "[config]") {
  auto result = eval_lua(R"(
    zedit.set_option("tabstop", 8)
    error("boom")
  )");
  REQUIRE_FALSE(result.errors.empty());
  REQUIRE(result.options.tabstop == 8);
}

TEST_CASE("eval_lua supports arbitrary Lua computation, not just static values",
          "[config]") {
  std::string src = R"(
    local base = 2
    for i = 1, 3 do
      zedit.set_color("token" .. i, string.format("#%02x%02x%02x", i * 10, i * 10, i * 10))
    end
    zedit.set_option("tabstop", base * 2)
  )";
  auto result = eval_lua(src);
  REQUIRE(result.errors.empty());
  REQUIRE(*result.options.tabstop == 4);
  REQUIRE(result.colors.size() == 3);
  REQUIRE(result.colors.at("token1").r == 10);
  REQUIRE(result.colors.at("token3").r == 30);
}

TEST_CASE("load_config_file returns a default result for a missing file", "[config]") {
  auto result = load_config_file("/tmp/zedit_test_config_does_not_exist_12345.lua");
  REQUIRE(result.errors.empty());
  REQUIRE(result.options.tabstop == std::nullopt);
  REQUIRE(result.colors.empty());
  REQUIRE(result.normal_remap.empty());
}

TEST_CASE("load_config_file loads and evaluates a real file", "[config]") {
  std::string path = "/tmp/zedit_test_config_real.lua";
  zedit::core::write_file(path, R"(zedit.set_option("tabstop", 3))");
  auto result = load_config_file(path);
  REQUIRE(result.errors.empty());
  REQUIRE(*result.options.tabstop == 3);
}

TEST_CASE("default_config_path honors ZEDIT_CONFIG", "[config]") {
  const char* saved = std::getenv("ZEDIT_CONFIG");
  std::string saved_copy = saved != nullptr ? saved : "";
  bool had_saved = saved != nullptr;

  setenv("ZEDIT_CONFIG", "/tmp/my_custom_zedit_config.lua", 1);
  REQUIRE(default_config_path() == "/tmp/my_custom_zedit_config.lua");

  if (had_saved) {
    setenv("ZEDIT_CONFIG", saved_copy.c_str(), 1);
  } else {
    unsetenv("ZEDIT_CONFIG");
  }
}
