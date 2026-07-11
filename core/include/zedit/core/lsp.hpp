#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "zedit/core/json_rpc.hpp"

namespace zedit::core {

enum class DiagnosticSeverity { Error = 1, Warning = 2, Information = 3, Hint = 4 };

struct LspDiagnostic {
  size_t start_line = 0;
  size_t start_col = 0;
  size_t end_line = 0;
  size_t end_col = 0;
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  std::string message;
};

struct LspLocation {
  std::string path;  // filesystem path (converted from a file:// URI)
  size_t line = 0;
  size_t col = 0;
};

// A thin client for one language server subprocess (clangd, for C++
// files, is the only one wired up so far -- see languages.cpp/lsp_for_*).
// Talks LSP over JsonRpcTransport. The initialize handshake must complete
// before anything else is sent, so calls made before then (e.g. opening
// the very first document right after start()) are queued and flushed
// once the server responds.
class LspManager {
 public:
  LspManager() = default;
  ~LspManager() = default;
  LspManager(LspManager&&) = default;
  LspManager& operator=(LspManager&&) = default;
  LspManager(const LspManager&) = delete;
  LspManager& operator=(const LspManager&) = delete;

  // `command` is searched via PATH (e.g. "clangd"). Returns false if the
  // process failed to spawn (e.g. not installed) -- callers should treat
  // that as "no LSP features available," not an error.
  bool start(const std::string& command);
  bool running() const { return transport_ && transport_->running(); }
  void stop();

  void open_document(const std::string& path, const std::string& text);
  void change_document(const std::string& path, const std::string& text);
  void close_document(const std::string& path);

  using DefinitionCallback = std::function<void(std::optional<LspLocation>)>;
  using HoverCallback = std::function<void(std::optional<std::string>)>;
  void request_definition(const std::string& path, size_t line, size_t col,
                           DefinitionCallback callback);
  void request_hover(const std::string& path, size_t line, size_t col, HoverCallback callback);

  std::vector<LspDiagnostic> diagnostics_for(const std::string& path) const;

  // Call once per frame from the main thread: drains incoming messages
  // and dispatches diagnostics/request callbacks.
  void poll();

 private:
  // Boxed because JsonRpcTransport's reader thread captures `this`; the
  // transport object's address must never change, so LspManager (and
  // Editor, which owns one) can still be freely moved -- only the
  // pointer moves, not the thread-owning object it points to.
  std::unique_ptr<JsonRpcTransport> transport_;
  bool initialized_ = false;
  int next_id_ = 1;
  std::unordered_map<std::string, int> document_versions_;
  std::unordered_map<std::string, std::vector<LspDiagnostic>> diagnostics_;
  std::unordered_map<int, std::function<void(const nlohmann::json&)>> pending_requests_;
  std::vector<std::function<void()>> deferred_until_initialized_;

  void run_or_defer(std::function<void()> action);
  void handle_message(const nlohmann::json& msg);
};

std::string lsp_path_to_uri(const std::string& path);
std::string lsp_uri_to_path(const std::string& uri);

}  // namespace zedit::core
