#pragma once

#include <string>

#include "zedit/core/mode.hpp"

namespace zedit::core {

class Editor;

// Owns the current editing mode and any state pending within it (e.g. the
// in-progress ':' command line). Dispatches by mode to a handler per mode
// rather than using a State-pattern class hierarchy, since vim's modal
// grammar is more naturally expressed as "mode + a little pending state"
// than as polymorphic mode objects.
class ModeStateMachine {
 public:
  KeyResult handle_key(KeyEvent ev, Editor& ed);

  Mode mode() const { return mode_; }
  const std::string& command_line_buffer() const {
    return command_line_buffer_;
  }
  const std::string& last_error() const { return last_error_; }

 private:
  Mode mode_ = Mode::Normal;
  std::string command_line_buffer_;
  std::string last_error_;

  KeyResult handle_normal(KeyEvent ev, Editor& ed);
  KeyResult handle_insert(KeyEvent ev, Editor& ed);
  KeyResult handle_command_line(KeyEvent ev, Editor& ed);
};

}  // namespace zedit::core
