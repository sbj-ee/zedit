#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include "zedit/core/json_rpc.hpp"

using zedit::core::JsonRpcTransport;

namespace {

// Polls until at least `count` messages have arrived or the timeout
// elapses, since the transport's reader thread delivers asynchronously.
std::vector<nlohmann::json> poll_until(JsonRpcTransport& transport, size_t count) {
  std::vector<nlohmann::json> collected;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (collected.size() < count && std::chrono::steady_clock::now() < deadline) {
    for (auto& m : transport.poll()) {
      collected.push_back(std::move(m));
    }
    if (collected.size() < count) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  return collected;
}

}  // namespace

TEST_CASE("JsonRpcTransport round-trips a message through a cat echo process", "[json_rpc][subprocess]") {
  JsonRpcTransport transport;
  REQUIRE(transport.start("/bin/cat", {}));
  REQUIRE(transport.running());

  nlohmann::json msg = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ping"}};
  transport.send(msg);

  auto received = poll_until(transport, 1);
  REQUIRE(received.size() == 1);
  REQUIRE(received[0] == msg);

  transport.stop();
  REQUIRE_FALSE(transport.running());
}

TEST_CASE("JsonRpcTransport delivers multiple messages sent back to back", "[json_rpc][subprocess]") {
  JsonRpcTransport transport;
  REQUIRE(transport.start("/bin/cat", {}));

  transport.send({{"id", 1}});
  transport.send({{"id", 2}});
  transport.send({{"id", 3}});

  auto received = poll_until(transport, 3);
  REQUIRE(received.size() == 3);
  REQUIRE(received[0]["id"] == 1);
  REQUIRE(received[1]["id"] == 2);
  REQUIRE(received[2]["id"] == 3);

  transport.stop();
}
