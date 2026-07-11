#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "zedit/core/update_check.hpp"

namespace zedit::frontend {

// Checks GitHub's "latest release" API in a background thread (shelling
// out to `curl` via zedit::core::Subprocess -- avoids pulling in a full
// HTTP client library just for one JSON GET) and reports back the next
// time poll() is called from the main thread.
//
// Deliberately NOT owned by Editor or App: both get move-constructed
// during startup (Editor::open_file()'s return value into App's member),
// and a thread that captures `this` can't survive its owner's address
// changing -- see LspManager's own transport_ boxing for the bug this
// caused there. Living instead as a plain main()-local (like the
// GLFWwindow* itself) that's never moved sidesteps the problem entirely
// rather than needing the same unique_ptr-boxing workaround again.
class UpdateChecker {
 public:
  UpdateChecker() = default;
  ~UpdateChecker();
  UpdateChecker(const UpdateChecker&) = delete;
  UpdateChecker& operator=(const UpdateChecker&) = delete;

  // Starts a background check against `repo` ("owner/name") if one isn't
  // already in flight. Safe to call repeatedly (e.g. a "Check for
  // Updates" menu item re-triggering it) -- a no-op while already
  // checking.
  void start_check(std::string current_version, std::string repo = "sbj-ee/zedit");

  // Call once per frame. Returns the update info the moment a check
  // completes and finds something newer; nullopt otherwise (still
  // checking, no update found, or the check failed -- e.g. no `curl`, no
  // network, or no releases published yet).
  std::optional<zedit::core::UpdateInfo> poll();

  bool checking() const { return checking_.load(); }

 private:
  std::thread thread_;
  std::atomic<bool> checking_{false};
  std::mutex result_mutex_;
  std::optional<std::string> result_json_;  // guarded by result_mutex_
  // Only ever touched from the main thread (set in start_check(), read in
  // poll()) -- both are called from the same render loop -- so this needs
  // no synchronization of its own, unlike result_json_ above.
  std::string current_version_;
};

}  // namespace zedit::frontend
