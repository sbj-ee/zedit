#include <catch2/catch_test_macros.hpp>

#include <string>

#include "zedit/core/json_rpc.hpp"

using zedit::core::frame_message;
using zedit::core::try_parse_message;

TEST_CASE("frame_message produces a well-formed Content-Length header", "[json_rpc]") {
  nlohmann::json msg = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}};
  std::string framed = frame_message(msg);
  std::string body = msg.dump();
  std::string expected_header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
  REQUIRE(framed == expected_header + body);
}

TEST_CASE("try_parse_message parses a single complete message", "[json_rpc]") {
  nlohmann::json msg = {{"jsonrpc", "2.0"}, {"method", "ping"}};
  std::string framed = frame_message(msg);

  auto result = try_parse_message(framed);
  REQUIRE(result.has_value());
  REQUIRE(result->message == msg);
  REQUIRE(result->bytes_consumed == framed.size());
}

TEST_CASE("try_parse_message returns nullopt when the body is incomplete", "[json_rpc]") {
  nlohmann::json msg = {{"jsonrpc", "2.0"}, {"method", "ping"}};
  std::string framed = frame_message(msg);
  std::string partial = framed.substr(0, framed.size() - 3);  // chop off the tail of the body

  auto result = try_parse_message(partial);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("try_parse_message returns nullopt when the header itself is incomplete", "[json_rpc]") {
  std::string partial_header = "Content-Length: 10\r\n";  // missing the blank line
  auto result = try_parse_message(partial_header);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("try_parse_message parses two consecutive messages using bytes_consumed", "[json_rpc]") {
  nlohmann::json msg_a = {{"id", 1}};
  nlohmann::json msg_b = {{"id", 2}};
  std::string buffer = frame_message(msg_a) + frame_message(msg_b);

  auto first = try_parse_message(buffer);
  REQUIRE(first.has_value());
  REQUIRE(first->message == msg_a);

  buffer.erase(0, first->bytes_consumed);
  auto second = try_parse_message(buffer);
  REQUIRE(second.has_value());
  REQUIRE(second->message == msg_b);
  REQUIRE(second->bytes_consumed == buffer.size());
}

TEST_CASE("try_parse_message tolerates extra headers before Content-Length", "[json_rpc]") {
  nlohmann::json msg = {{"ok", true}};
  std::string body = msg.dump();
  std::string framed = "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n"
                        "Content-Length: " +
                        std::to_string(body.size()) + "\r\n\r\n" + body;

  auto result = try_parse_message(framed);
  REQUIRE(result.has_value());
  REQUIRE(result->message == msg);
}

TEST_CASE("try_parse_message consumes a malformed body instead of stalling", "[json_rpc]") {
  std::string bad_body = "{not valid json";
  std::string framed =
      "Content-Length: " + std::to_string(bad_body.size()) + "\r\n\r\n" + bad_body;

  auto result = try_parse_message(framed);
  REQUIRE(result.has_value());
  REQUIRE(result->bytes_consumed == framed.size());
}
