#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

TEST_CASE("nlohmann::json links and round-trips basic values", "[json]") {
  nlohmann::json j;
  j["jsonrpc"] = "2.0";
  j["id"] = 1;
  j["method"] = "initialize";

  std::string serialized = j.dump();
  nlohmann::json parsed = nlohmann::json::parse(serialized);

  REQUIRE(parsed["jsonrpc"] == "2.0");
  REQUIRE(parsed["id"] == 1);
  REQUIRE(parsed["method"] == "initialize");
}
