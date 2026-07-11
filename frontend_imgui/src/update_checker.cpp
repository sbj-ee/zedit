#include "update_checker.hpp"

#include "zedit/core/subprocess.hpp"

namespace zedit::frontend {

UpdateChecker::~UpdateChecker() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void UpdateChecker::start_check(std::string current_version, std::string repo) {
  if (checking_.load()) {
    return;
  }
  if (thread_.joinable()) {
    thread_.join();  // clean up the finished previous check's thread handle
  }

  current_version_ = std::move(current_version);
  checking_.store(true);
  std::string url = "https://api.github.com/repos/" + repo + "/releases/latest";

  thread_ = std::thread([this, url]() {
    zedit::core::Subprocess proc;
    std::string output;
    // --max-time bounds how long this can possibly run: without it, a
    // hung DNS lookup or dead connection could block shutdown (the
    // destructor joins this thread) for an arbitrarily long time.
    if (proc.start("curl", {"-s", "--max-time", "5", "-A", "zedit-update-check", url})) {
      std::string chunk;
      while (!(chunk = proc.read_some()).empty()) {
        output += chunk;
      }
    }
    {
      std::lock_guard<std::mutex> lock(result_mutex_);
      result_json_ = std::move(output);
    }
    checking_.store(false);
  });
}

std::optional<zedit::core::UpdateInfo> UpdateChecker::poll() {
  std::optional<std::string> json;
  {
    std::lock_guard<std::mutex> lock(result_mutex_);
    json = std::move(result_json_);
    result_json_.reset();
  }
  if (!json.has_value()) {
    return std::nullopt;
  }
  return zedit::core::parse_newer_version(current_version_, *json);
}

}  // namespace zedit::frontend
