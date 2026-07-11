#include "zedit/core/lsp.hpp"

namespace zedit::core {

std::string lsp_path_to_uri(const std::string& path) { return "file://" + path; }

std::string lsp_uri_to_path(const std::string& uri) {
  constexpr std::string_view kPrefix = "file://";
  if (uri.size() >= kPrefix.size() && uri.substr(0, kPrefix.size()) == kPrefix) {
    return uri.substr(kPrefix.size());
  }
  return uri;
}

bool LspManager::start(const std::string& command) {
  transport_ = std::make_unique<JsonRpcTransport>();
  if (!transport_->start(command, {})) {
    transport_.reset();
    return false;
  }

  nlohmann::json params;
  params["processId"] = nullptr;
  params["rootUri"] = nullptr;
  params["capabilities"] = nlohmann::json::object();

  int id = next_id_++;
  pending_requests_[id] = [this](const nlohmann::json&) {
    initialized_ = true;
    transport_->send(
        {{"jsonrpc", "2.0"}, {"method", "initialized"}, {"params", nlohmann::json::object()}});
    std::vector<std::function<void()>> deferred = std::move(deferred_until_initialized_);
    deferred_until_initialized_.clear();
    for (auto& action : deferred) {
      action();
    }
  };
  transport_->send(
      {{"jsonrpc", "2.0"}, {"id", id}, {"method", "initialize"}, {"params", params}});
  return true;
}

void LspManager::stop() {
  if (transport_) {
    transport_->stop();
  }
  initialized_ = false;
  document_versions_.clear();
  diagnostics_.clear();
  pending_requests_.clear();
  deferred_until_initialized_.clear();
}

void LspManager::run_or_defer(std::function<void()> action) {
  if (initialized_) {
    action();
  } else {
    deferred_until_initialized_.push_back(std::move(action));
  }
}

void LspManager::open_document(const std::string& path, const std::string& text) {
  run_or_defer([this, path, text]() {
    document_versions_[path] = 1;
    nlohmann::json params;
    params["textDocument"] = {
        {"uri", lsp_path_to_uri(path)}, {"languageId", "cpp"}, {"version", 1}, {"text", text}};
    transport_->send(
        {{"jsonrpc", "2.0"}, {"method", "textDocument/didOpen"}, {"params", params}});
  });
}

void LspManager::change_document(const std::string& path, const std::string& text) {
  run_or_defer([this, path, text]() {
    int version = ++document_versions_[path];
    nlohmann::json params;
    params["textDocument"] = {{"uri", lsp_path_to_uri(path)}, {"version", version}};
    params["contentChanges"] = nlohmann::json::array({{{"text", text}}});
    transport_->send(
        {{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"}, {"params", params}});
  });
}

void LspManager::close_document(const std::string& path) {
  run_or_defer([this, path]() {
    document_versions_.erase(path);
    diagnostics_.erase(path);
    nlohmann::json params;
    params["textDocument"] = {{"uri", lsp_path_to_uri(path)}};
    transport_->send(
        {{"jsonrpc", "2.0"}, {"method", "textDocument/didClose"}, {"params", params}});
  });
}

void LspManager::request_definition(const std::string& path, size_t line, size_t col,
                                     DefinitionCallback callback) {
  run_or_defer([this, path, line, col, callback]() {
    int id = next_id_++;
    nlohmann::json params;
    params["textDocument"] = {{"uri", lsp_path_to_uri(path)}};
    params["position"] = {{"line", line}, {"character", col}};

    pending_requests_[id] = [callback](const nlohmann::json& result) {
      if (result.is_null() || (result.is_array() && result.empty())) {
        callback(std::nullopt);
        return;
      }
      const nlohmann::json& loc = result.is_array() ? result[0] : result;

      std::string uri;
      nlohmann::json range;
      if (loc.contains("uri")) {
        uri = loc.value("uri", "");
        range = loc.value("range", nlohmann::json::object());
      } else if (loc.contains("targetUri")) {
        uri = loc.value("targetUri", "");
        range = loc.contains("targetSelectionRange") ? loc["targetSelectionRange"]
                                                       : loc.value("targetRange", nlohmann::json::object());
      }
      if (uri.empty() || !range.contains("start")) {
        callback(std::nullopt);
        return;
      }
      size_t result_line = range["start"].value("line", static_cast<size_t>(0));
      size_t result_col = range["start"].value("character", static_cast<size_t>(0));
      callback(LspLocation{lsp_uri_to_path(uri), result_line, result_col});
    };

    transport_->send({{"jsonrpc", "2.0"},
                      {"id", id},
                      {"method", "textDocument/definition"},
                      {"params", params}});
  });
}

void LspManager::request_hover(const std::string& path, size_t line, size_t col,
                                HoverCallback callback) {
  run_or_defer([this, path, line, col, callback]() {
    int id = next_id_++;
    nlohmann::json params;
    params["textDocument"] = {{"uri", lsp_path_to_uri(path)}};
    params["position"] = {{"line", line}, {"character", col}};

    pending_requests_[id] = [callback](const nlohmann::json& result) {
      if (result.is_null()) {
        callback(std::nullopt);
        return;
      }
      nlohmann::json contents = result.value("contents", nlohmann::json());
      std::string text;
      if (contents.is_string()) {
        text = contents.get<std::string>();
      } else if (contents.is_object()) {
        text = contents.value("value", "");
      } else if (contents.is_array()) {
        for (const nlohmann::json& item : contents) {
          if (item.is_string()) {
            text += item.get<std::string>();
          } else if (item.is_object()) {
            text += item.value("value", "");
          }
          text += "\n";
        }
      }
      if (text.empty()) {
        callback(std::nullopt);
        return;
      }
      callback(text);
    };

    transport_->send(
        {{"jsonrpc", "2.0"}, {"id", id}, {"method", "textDocument/hover"}, {"params", params}});
  });
}

std::vector<LspDiagnostic> LspManager::diagnostics_for(const std::string& path) const {
  auto it = diagnostics_.find(path);
  return it != diagnostics_.end() ? it->second : std::vector<LspDiagnostic>{};
}

void LspManager::poll() {
  if (!transport_) {
    return;
  }
  for (const nlohmann::json& msg : transport_->poll()) {
    handle_message(msg);
  }
}

void LspManager::handle_message(const nlohmann::json& msg) {
  if (msg.contains("id") && (msg.contains("result") || msg.contains("error"))) {
    int id = msg["id"].get<int>();
    auto it = pending_requests_.find(id);
    if (it != pending_requests_.end()) {
      it->second(msg.contains("result") ? msg["result"] : nlohmann::json());
      pending_requests_.erase(it);
    }
    return;
  }

  if (!msg.contains("method")) {
    return;
  }
  std::string method = msg.value("method", "");
  if (method != "textDocument/publishDiagnostics") {
    return;  // other server notifications/requests are ignored for now
  }

  nlohmann::json params = msg.value("params", nlohmann::json::object());
  std::string path = lsp_uri_to_path(params.value("uri", ""));
  std::vector<LspDiagnostic> diags;
  for (const nlohmann::json& d : params.value("diagnostics", nlohmann::json::array())) {
    LspDiagnostic diag;
    nlohmann::json range = d.value("range", nlohmann::json::object());
    diag.start_line = range["start"].value("line", static_cast<size_t>(0));
    diag.start_col = range["start"].value("character", static_cast<size_t>(0));
    diag.end_line = range["end"].value("line", static_cast<size_t>(0));
    diag.end_col = range["end"].value("character", static_cast<size_t>(0));
    diag.severity = static_cast<DiagnosticSeverity>(d.value("severity", 1));
    diag.message = d.value("message", "");
    diags.push_back(std::move(diag));
  }
  diagnostics_[path] = std::move(diags);
}

}  // namespace zedit::core
