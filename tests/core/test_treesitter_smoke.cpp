#include <catch2/catch_test_macros.hpp>

#include <tree_sitter/api.h>

TEST_CASE("tree-sitter core library links and a parser can be created", "[treesitter]") {
  TSParser* parser = ts_parser_new();
  REQUIRE(parser != nullptr);
  ts_parser_delete(parser);
}
