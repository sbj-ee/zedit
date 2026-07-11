#include "zedit/core/json_rpc.hpp"

namespace zedit::core {

std::string frame_message(const nlohmann::json& message) {
  std::string body = message.dump();
  std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
  return header + body;
}

std::optional<ParsedMessage> try_parse_message(std::string_view buffer) {
  size_t header_end = buffer.find("\r\n\r\n");
  if (header_end == std::string_view::npos) {
    return std::nullopt;
  }

  std::string_view headers = buffer.substr(0, header_end);
  std::optional<size_t> content_length;
  size_t pos = 0;
  while (pos < headers.size()) {
    size_t line_end = headers.find("\r\n", pos);
    if (line_end == std::string_view::npos) {
      line_end = headers.size();
    }
    std::string_view line = headers.substr(pos, line_end - pos);

    constexpr std::string_view kPrefix = "Content-Length:";
    if (line.size() > kPrefix.size() && line.substr(0, kPrefix.size()) == kPrefix) {
      std::string_view value = line.substr(kPrefix.size());
      while (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
      }
      size_t n = 0;
      for (char c : value) {
        if (c < '0' || c > '9') break;
        n = n * 10 + static_cast<size_t>(c - '0');
      }
      content_length = n;
    }
    pos = line_end + 2;
  }

  if (!content_length) {
    return std::nullopt;
  }

  size_t body_start = header_end + 4;
  if (buffer.size() < body_start + *content_length) {
    return std::nullopt;  // haven't received the full body yet
  }

  std::string_view body = buffer.substr(body_start, *content_length);
  nlohmann::json parsed = nlohmann::json::object();
  try {
    parsed = nlohmann::json::parse(body);
  } catch (const nlohmann::json::parse_error&) {
    // Malformed body: still consume the framed bytes below so a corrupt
    // message can't stall the reader forever.
  }
  return ParsedMessage{std::move(parsed), body_start + *content_length};
}

JsonRpcTransport::~JsonRpcTransport() { stop(); }

bool JsonRpcTransport::start(const std::string& command, const std::vector<std::string>& args) {
  if (!process_.start(command, args)) {
    return false;
  }
  stop_requested_ = false;
  reader_thread_ = std::thread(&JsonRpcTransport::reader_loop, this);
  return true;
}

void JsonRpcTransport::stop() {
  stop_requested_ = true;
  // Closing the pipes unblocks any in-progress read in the reader thread.
  process_.stop();
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }
}

void JsonRpcTransport::send(const nlohmann::json& message) { process_.write(frame_message(message)); }

std::vector<nlohmann::json> JsonRpcTransport::poll() {
  std::vector<nlohmann::json> result;
  std::lock_guard<std::mutex> lock(queue_mutex_);
  while (!incoming_.empty()) {
    result.push_back(std::move(incoming_.front()));
    incoming_.pop_front();
  }
  return result;
}

void JsonRpcTransport::reader_loop() {
  std::string buffer;
  while (!stop_requested_) {
    std::string chunk = process_.read_some();
    if (chunk.empty()) {
      break;  // process exited or pipe closed
    }
    buffer += chunk;
    while (true) {
      std::optional<ParsedMessage> parsed = try_parse_message(buffer);
      if (!parsed) {
        break;
      }
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        incoming_.push_back(std::move(parsed->message));
      }
      buffer.erase(0, parsed->bytes_consumed);
    }
  }
}

}  // namespace zedit::core
