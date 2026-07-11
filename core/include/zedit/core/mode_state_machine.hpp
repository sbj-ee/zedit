#pragma once

#include <optional>
#include <string>

#include "zedit/core/cursor.hpp"
#include "zedit/core/mode.hpp"

namespace zedit::core {

class Editor;

// Owns the current editing mode and any state pending within it: the
// in-progress ':' command line, or a Normal-mode operator (d/y/c) waiting
// on a count/motion/text-object to complete it. Dispatches by mode to a
// handler per mode rather than using a State-pattern class hierarchy,
// since vim's modal grammar is more naturally expressed as "mode + a
// little pending state" than as polymorphic mode objects. Operator+motion
// composition ("d" then "w") is assembled as data in PendingCommand and
// resolved once a motion, text object, or the operator's own doubled key
// (dd/yy/cc) completes the sequence.
class ModeStateMachine {
 public:
  KeyResult handle_key(KeyEvent ev, Editor& ed);

  Mode mode() const { return mode_; }
  const std::string& command_line_buffer() const {
    return command_line_buffer_;
  }
  const std::string& last_error() const { return last_error_; }
  Cursor visual_anchor() const { return visual_anchor_; }

 private:
  struct PendingCommand {
    int count = 0;
    std::optional<OperatorKind> op;
    bool awaiting_inner_object = false;
  };

  Mode mode_ = Mode::Normal;
  std::string command_line_buffer_;
  std::string last_error_;
  PendingCommand pending_;
  Cursor visual_anchor_{};

  KeyResult handle_normal(KeyEvent ev, Editor& ed);
  KeyResult handle_insert(KeyEvent ev, Editor& ed);
  KeyResult handle_command_line(KeyEvent ev, Editor& ed);
  KeyResult handle_visual(KeyEvent ev, Editor& ed);

  // Shared by Normal and Visual: h/l/j/k/w/b/e/0/$ move the cursor by
  // `repeat` steps (Normal) or extend the selection (Visual, repeat==1).
  bool apply_plain_motion(char ch, Editor& ed, int repeat);

  void enter_visual(Mode which, Editor& ed);
  void finish_operator_on_visual_selection(OperatorKind op, Editor& ed);
  void finish_pending_operator(char ch, Editor& ed);
  void reset_pending();
};

}  // namespace zedit::core
