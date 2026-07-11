#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "zedit/core/subprocess.hpp"

namespace zedit::core {

// Encodes a JSON-RPC message using LSP's stdio framing:
// "Content-Length: N\r\n\r\n" followed by N bytes of UTF-8 JSON.
std::string frame_message(const nlohmann::json& message);

struct ParsedMessage {
  nlohmann::json message;
  size_t bytes_consumed;
};

// Tries to parse one complete framed message from the front of `buffer`.
// Returns nullopt if `buffer` doesn't yet contain a complete message (the
// caller should read more bytes and retry). A malformed-but-complete body
// still reports bytes_consumed (with an empty JSON object as the result)
// so a corrupt message can't stall the reader forever waiting for bytes
// that won't fix it.
std::optional<ParsedMessage> try_parse_message(std::string_view buffer);

// Speaks LSP's JSON-RPC-over-stdio framing with a child process. A
// background thread reads and frames incoming messages into a
// thread-safe queue; poll() (called from the main thread once per frame)
// drains it. Writing is synchronous from the caller's thread -- fine
// since LSP requests are small and pipe writes rarely block for long.
class JsonRpcTransport {
 public:
  ~JsonRpcTransport();

  bool start(const std::string& command, const std::vector<std::string>& args);
  void stop();
  bool running() const { return process_.running(); }

  void send(const nlohmann::json& message);

  // Returns all complete messages received since the last call.
  std::vector<nlohmann::json> poll();

 private:
  Subprocess process_;
  std::thread reader_thread_;
  std::atomic<bool> stop_requested_{false};

  std::mutex queue_mutex_;
  std::deque<nlohmann::json> incoming_;

  void reader_loop();
};

}  // namespace zedit::core
