#pragma once

// Linux only. Reads the Wayland clipboard through wl-clipboard's `wl-paste`
// command (an optional runtime helper; see linux_clipboard.hpp for why).

#include <cstddef>
#include <optional>
#include <utility>
#include <string>
#include <vector>

#include "zedit/core/clipboard_source.hpp"

namespace zedit::frontend {

// `wl-paste --list-types` for targets(), `wl-paste --no-newline --type T`
// for read(). Each run is bounded by `timeout_ms` (then the child is killed),
// so an owner that never sends its data can't hang the UI; after a timeout
// the rest of that paste is abandoned (ReadStatus::timed_out).
class WlPasteClipboard final : public zedit::core::MimeClipboardSource {
 public:
  struct Options {
    std::string program = "wl-paste";
    int timeout_ms = 1000;
    size_t max_bytes = 64u << 20;
  };

  WlPasteClipboard() = default;
  explicit WlPasteClipboard(Options options) : options_(std::move(options)) {}

  // Whether `program` could be started at all (false: wl-clipboard isn't
  // installed). Only meaningful after the first targets()/read().
  bool program_missing() const { return program_missing_; }

  std::optional<std::vector<std::string>> targets() override;
  Read read(const std::string& type) override;

 private:
  struct Run {
    bool started = false;
    bool timed_out = false;
    int exit_code = -1;
    std::string out;
  };
  Run run(const std::vector<std::string>& args);

  Options options_;
  bool program_missing_ = false;
};

// Whether `name` resolves to an executable through $PATH.
bool program_on_path(const std::string& name);

}  // namespace zedit::frontend
