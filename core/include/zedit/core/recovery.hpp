#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace zedit::core {

// On-disk baseline of the real file at last successful load or save.
// Used when writing swap headers and when deciding whether a swap is stale.
struct Baseline {
  int64_t mtime_ns = 0;   // 0 if unknown
  uint64_t size = 0;
  uint64_t inode = 0;     // 0 if unavailable
};

// Parsed v1 swap payload (see docs/crash-recovery.md).
struct SwapPayload {
  std::string path;
  Baseline baseline;
  int64_t saved_at_ns = 0;
  uint32_t cursor_line = 0;
  uint32_t cursor_col = 0;
  std::string content;
};

// Pending reopen prompt: core queues the offer; the frontend owns the modal.
struct RecoveryOffer {
  std::string path;
  std::string content;
};

// Linux: $XDG_CACHE_HOME/zedit/swap or ~/.cache/zedit/swap
// macOS: ~/Library/Application Support/zedit/swap
// Override: $ZEDIT_SWAP_DIR (useful for tests).
std::string default_swap_dir();

// sha256(utf8 absolute path) hex + ".swp" under the swap dir.
std::string swap_path_for(const std::string& abs_path);

// Resolve to a stable absolute path (weakly_canonical when possible).
std::string absolute_path_for_swap(const std::string& path);

Baseline baseline_for_path(const std::string& path);

// Atomic write: *.swp.tmp then rename. Returns false on size refusal / I/O failure.
bool write_swap_atomic(const std::string& abs_path, std::string_view content,
                       const Baseline& baseline);

// Returns nullopt if missing, corrupt, truncated, or over kMaxFileSizeBytes
// (corrupt/truncated files are deleted).
std::optional<SwapPayload> read_swap(const std::string& abs_path);

void clear_swap(const std::string& abs_path);

// After a successful open/load: look up the swap for `abs_path`. Identical
// or otherwise-stale swaps are cleared and return nullopt; otherwise returns
// a RecoveryOffer the frontend can prompt on (does not mutate the buffer).
std::optional<RecoveryOffer> consider_recovery(const std::string& abs_path,
                                               std::string_view disk_content);

// Debounce helper: arm() on each dirty edit; poll() returns true once the
// quiet period (default 2s) has elapsed since the last arm.
class RecoveryDebouncer {
 public:
  static constexpr std::chrono::milliseconds kDefaultDelay{2000};

  void arm(std::chrono::milliseconds delay = kDefaultDelay);
  bool poll();
  void clear();
  bool armed() const { return due_.has_value(); }

 private:
  std::optional<std::chrono::steady_clock::time_point> due_;
};

}  // namespace zedit::core
