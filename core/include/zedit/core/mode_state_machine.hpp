#pragma once

#include <optional>
#include <string>
#include <unordered_map>

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

  // Non-recursive Normal-mode key remapping (vim's :noremap, not :map),
  // from a config script's zedit.map("n", lhs, rhs). Only applies to a
  // key at the very start of a fresh command (no pending count/operator/
  // register), matching the scope of a "minimal" scripting hook rather
  // than vim's full remapping semantics (which also compose with counts
  // and other pending state). Merges into any existing mappings rather
  // than replacing them, so a later ":lua zedit.map(...)" call adds to
  // -- rather than wiping out -- whatever the startup config set up.
  void set_normal_remap(const std::unordered_map<char, std::string>& remap) {
    for (const auto& [lhs, rhs] : remap) {
      normal_remap_[lhs] = rhs;
    }
  }

  // Enters Visual mode selecting the word (or punctuation/whitespace run)
  // under the current cursor position -- the same run "viw" would select.
  // Used by the frontend's double-click handling; a no-op on an empty
  // buffer. Public (unlike select_all, only reachable via Ctrl-A) since
  // there's no KeyEvent for "the user double-clicked here" to route it
  // through handle_key.
  void select_word_at_cursor(Editor& ed);

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
  std::unordered_map<char, std::string> normal_remap_;
  bool replaying_remap_ = false;

  KeyResult handle_normal(KeyEvent ev, Editor& ed);
  KeyResult handle_insert(KeyEvent ev, Editor& ed);
  KeyResult handle_command_line(KeyEvent ev, Editor& ed);
  KeyResult handle_visual(KeyEvent ev, Editor& ed);
  KeyResult handle_search(KeyEvent ev, Editor& ed);

  // Shared by Normal and Visual: h/l/j/k/w/b/e/0/$ move the cursor by
  // `repeat` steps (Normal) or extend the selection (Visual, repeat==1).
  bool apply_plain_motion(char ch, Editor& ed, int repeat);

  // Ctrl-A, handled at the top of handle_key regardless of mode (Normal,
  // Insert, or Visual/VisualLine -- but not CommandLine/Search, where
  // there's no document selection to speak of): selects the whole buffer
  // as a linewise Visual selection, matching a GUI editor's "select all"
  // rather than vim's own Ctrl-A (increment number under cursor), which
  // zedit doesn't implement.
  void select_all(Editor& ed);
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
