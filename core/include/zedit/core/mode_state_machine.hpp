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
  const std::string& last_error() const { return last_error_; }
  Cursor visual_anchor() const { return visual_anchor_; }

  // The line-input buffer shared by CommandLine and Search modes, and the
  // prefix character (':', '/', or '?') that says which one is active --
  // used by the status line to render the prompt.
  const std::string& command_line_buffer() const { return command_line_buffer_; }
  char line_input_prefix() const { return line_input_prefix_; }

 private:
  struct PendingCommand {
    int count = 0;
    std::optional<OperatorKind> op;
    bool awaiting_inner_object = false;
    bool awaiting_register_name = false;
    char register_name = 0;  // 0 = unnamed
    bool awaiting_g_command = false;  // 'g' prefix, e.g. "gd" (go to definition)
  };

  // What '.' replays. Kinds that enter Insert mode (PlainInsert, and any
  // operator kind paired with OperatorKind::Change) can't be recorded until
  // the insert session ends, so they're staged in pending_change_ when
  // Insert mode is entered and finalized (typed_text filled in) on Escape.
  enum class ChangeKind {
    None,
    DeleteChar,
    Paste,
    CharwiseOperator,
    LinewiseOperator,
    TextObjectOperator,
    PlainInsert,
  };
  struct LastChange {
    ChangeKind kind = ChangeKind::None;
    OperatorKind op = OperatorKind::Delete;
    char motion_ch = 0;      // for CharwiseOperator
    char insert_trigger = 0; // for PlainInsert: 'i'/'a'/'o'/'O'
    int count = 1;
    bool paste_before = false;
    char register_name = 0;
    std::string typed_text;
  };

  Mode mode_ = Mode::Normal;
  std::string command_line_buffer_;
  char line_input_prefix_ = ':';
  std::string last_error_;
  PendingCommand pending_;
  Cursor visual_anchor_{};
  LastChange last_change_;
  std::optional<LastChange> pending_change_;
  std::string insert_session_text_;

  KeyResult handle_normal(KeyEvent ev, Editor& ed);
  KeyResult handle_insert(KeyEvent ev, Editor& ed);
  KeyResult handle_command_line(KeyEvent ev, Editor& ed);
  KeyResult handle_visual(KeyEvent ev, Editor& ed);
  KeyResult handle_search(KeyEvent ev, Editor& ed);

  // Shared by Normal and Visual: h/l/j/k/w/b/e/0/$ move the cursor by
  // `repeat` steps (Normal) or extend the selection (Visual, repeat==1).
  bool apply_plain_motion(char ch, Editor& ed, int repeat);

  void enter_visual(Mode which, Editor& ed);
  void finish_operator_on_visual_selection(OperatorKind op, Editor& ed);
  void finish_pending_operator(char ch, Editor& ed);
  void reset_pending();

  // Shared by finish_pending_operator and repeat_last_change: resolves a
  // charwise motion key (w/b/e/0/$/h/l) to a target offset `b` and whether
  // it's inclusive of that offset, starting from the current cursor.
  bool resolve_charwise_motion(char ch, OperatorKind op, int count, Editor& ed,
                                size_t& b, bool& inclusive);
  void replay_insert_text(const std::string& text, Editor& ed);
  void repeat_last_change(Editor& ed);
  void begin_insert_change(LastChange lc);
};

}  // namespace zedit::core
