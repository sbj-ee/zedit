#include "zedit/core/mode_state_machine.hpp"

#include <algorithm>
#include <utility>

#include "zedit/core/command_line.hpp"
#include "zedit/core/config.hpp"
#include "zedit/core/editor.hpp"
#include "zedit/core/file_io.hpp"
#include "zedit/core/motion.hpp"
#include "zedit/core/operator.hpp"
#include "zedit/core/search.hpp"

namespace zedit::core {

namespace {

size_t offset_left_within_line(Editor& ed, size_t offset) {
  size_t line = ed.offset_to_cursor(offset).line;
  size_t line_start = ed.buffer().line_start_offset(line);
  return offset > line_start ? offset - 1 : line_start;
}

size_t offset_right_within_line(Editor& ed, size_t offset) {
  Cursor c = ed.offset_to_cursor(offset);
  size_t line_start = ed.buffer().line_start_offset(c.line);
  size_t max_offset = line_start + ed.buffer().line_text(c.line).size();
  return offset < max_offset ? offset + 1 : max_offset;
}

}  // namespace

KeyResult ModeStateMachine::handle_key(KeyEvent ev, Editor& ed) {
  if (ev.key == Key::CtrlA && mode_ != Mode::CommandLine && mode_ != Mode::Search) {
    select_all(ed);
    return KeyResult{};
  }
  if (ev.key == Key::CtrlS) {
    // Global save shortcut -- works from any mode (including mid-typing
    // in Insert, like every other GUI editor's Ctrl-S) without leaving it.
    try {
      ed.save();
    } catch (const FileIoError& e) {
      last_error_ = e.what();
    }
    return KeyResult{};
  }
  switch (mode_) {
    case Mode::Normal:
      return handle_normal(ev, ed);
    case Mode::Insert:
      return handle_insert(ev, ed);
    case Mode::CommandLine:
      return handle_command_line(ev, ed);
    case Mode::Visual:
    case Mode::VisualLine:
      return handle_visual(ev, ed);
    case Mode::Search:
      return handle_search(ev, ed);
  }
  return KeyResult{};
}

void ModeStateMachine::reset_pending() { pending_ = PendingCommand{}; }

bool ModeStateMachine::apply_plain_motion(char ch, Editor& ed, int repeat) {
  repeat = std::max(repeat, 1);
  switch (ch) {
    case 'h':
      for (int i = 0; i < repeat; ++i) ed.move_left();
      return true;
    case 'l':
      for (int i = 0; i < repeat; ++i) ed.move_right();
      return true;
    case 'k':
      for (int i = 0; i < repeat; ++i) ed.move_up();
      return true;
    case 'j':
      for (int i = 0; i < repeat; ++i) ed.move_down();
      return true;
    case 'w': {
      size_t offset = ed.cursor_offset();
      for (int i = 0; i < repeat; ++i) offset = motion_word_forward(ed.buffer(), offset);
      ed.set_cursor(ed.offset_to_cursor(offset));
      ed.clamp_cursor_to_line();
      return true;
    }
    case 'b': {
      size_t offset = ed.cursor_offset();
      for (int i = 0; i < repeat; ++i) offset = motion_word_backward(ed.buffer(), offset);
      ed.set_cursor(ed.offset_to_cursor(offset));
      return true;
    }
    case 'e': {
      size_t offset = ed.cursor_offset();
      for (int i = 0; i < repeat; ++i) offset = motion_word_end_forward(ed.buffer(), offset);
      ed.set_cursor(ed.offset_to_cursor(offset));
      return true;
    }
    case '0': {
      size_t offset = motion_line_start(ed.buffer(), ed.cursor_offset());
      ed.set_cursor(ed.offset_to_cursor(offset));
      return true;
    }
    case '$': {
      size_t offset = motion_line_end(ed.buffer(), ed.cursor_offset());
      ed.set_cursor(ed.offset_to_cursor(offset));
      return true;
    }
    default:
      return false;
  }
}

bool ModeStateMachine::resolve_charwise_motion(char ch, OperatorKind op, int count,
                                                Editor& ed, size_t& b, bool& inclusive) {
  switch (ch) {
    case 'w':
      // Real vim's "cw" acts like "ce" (stops at the end of the word rather
      // than eating the trailing whitespace that plain "w" would consume) --
      // otherwise "cwfoo" on "bar baz" would leave a stray leading space.
      if (op == OperatorKind::Change) {
        for (int i = 0; i < count; ++i) b = motion_word_end_forward(ed.buffer(), b);
        inclusive = true;
      } else {
        for (int i = 0; i < count; ++i) b = motion_word_forward(ed.buffer(), b);
      }
      return true;
    case 'b':
      for (int i = 0; i < count; ++i) b = motion_word_backward(ed.buffer(), b);
      return true;
    case 'e':
      for (int i = 0; i < count; ++i) b = motion_word_end_forward(ed.buffer(), b);
      inclusive = true;
      return true;
    case '0':
      b = motion_line_start(ed.buffer(), b);
      return true;
    case '$':
      b = motion_line_end(ed.buffer(), b);
      inclusive = true;
      return true;
    case 'h':
      for (int i = 0; i < count; ++i) b = offset_left_within_line(ed, b);
      return true;
    case 'l':
      for (int i = 0; i < count; ++i) b = offset_right_within_line(ed, b);
      return true;
    default:
      return false;
  }
}

void ModeStateMachine::finish_pending_operator(char ch, Editor& ed) {
  OperatorKind op = *pending_.op;
  int count = pending_.count > 0 ? pending_.count : 1;
  char register_name = pending_.register_name;

  if (pending_.awaiting_inner_object) {
    if (ch == 'w') {
      Range r = text_object_inner_word(ed.buffer(), ed.cursor_offset());
      execute_operator_charwise(op, r, ed, register_name);
      if (op == OperatorKind::Delete) {
        ed.clamp_cursor_to_line();
        last_change_ = LastChange{ChangeKind::TextObjectOperator, op, 0, 0, 1, false, register_name, {}};
      } else if (op == OperatorKind::Change) {
        mode_ = Mode::Insert;
        begin_insert_change(LastChange{ChangeKind::TextObjectOperator, op, 0, 0, 1, false, register_name, {}});
      }
    }
    reset_pending();
    return;
  }

  if (ch == 'i') {
    pending_.awaiting_inner_object = true;
    return;
  }

  bool doubled = (op == OperatorKind::Delete && ch == 'd') ||
                 (op == OperatorKind::Yank && ch == 'y') ||
                 (op == OperatorKind::Change && ch == 'c');
  if (doubled) {
    execute_operator_linewise(op, ed.cursor().line, count, ed, register_name);
    if (op == OperatorKind::Change) {
      mode_ = Mode::Insert;
      begin_insert_change(LastChange{ChangeKind::LinewiseOperator, op, 0, 0, count, false, register_name, {}});
    } else if (op == OperatorKind::Delete) {
      last_change_ = LastChange{ChangeKind::LinewiseOperator, op, 0, 0, count, false, register_name, {}};
    }
    reset_pending();
    return;
  }

  size_t a = ed.cursor_offset();
  size_t b = a;
  bool inclusive = false;
  bool valid_motion = resolve_charwise_motion(ch, op, count, ed, b, inclusive);

  if (valid_motion) {
    size_t lo = std::min(a, b);
    size_t hi = std::max(a, b);
    if (inclusive) hi = std::min(hi + 1, ed.buffer().size());
    execute_operator_charwise(op, Range{lo, hi}, ed, register_name);
    if (op == OperatorKind::Delete) {
      ed.clamp_cursor_to_line();
      last_change_ = LastChange{ChangeKind::CharwiseOperator, op, ch, 0, count, false, register_name, {}};
    } else if (op == OperatorKind::Change) {
      mode_ = Mode::Insert;
      begin_insert_change(LastChange{ChangeKind::CharwiseOperator, op, ch, 0, count, false, register_name, {}});
    }
  }
  reset_pending();
}

void ModeStateMachine::select_all(Editor& ed) {
  reset_pending();
  mode_ = Mode::VisualLine;
  visual_anchor_ = Cursor{0, 0};
  size_t total_lines = ed.buffer().line_count();
  size_t last_line = total_lines > 0 ? total_lines - 1 : 0;
  ed.set_cursor(Cursor{last_line, 0});
}

void ModeStateMachine::enter_visual(Mode which, Editor& ed) {
  visual_anchor_ = ed.cursor();
  mode_ = which;
}

void ModeStateMachine::finish_operator_on_visual_selection(OperatorKind op, Editor& ed) {
  bool linewise = (mode_ == Mode::VisualLine);
  Cursor a = visual_anchor_;
  Cursor b = ed.cursor();
  mode_ = Mode::Normal;

  if (linewise) {
    size_t start_line = std::min(a.line, b.line);
    size_t end_line = std::max(a.line, b.line);
    int count = static_cast<int>(end_line - start_line + 1);
    execute_operator_linewise(op, start_line, count, ed);
  } else {
    size_t offset_a = ed.buffer().line_col_to_offset(a.line, a.col);
    size_t offset_b = ed.buffer().line_col_to_offset(b.line, b.col);
    size_t lo = std::min(offset_a, offset_b);
    size_t hi = std::min(std::max(offset_a, offset_b) + 1, ed.buffer().size());
    execute_operator_charwise(op, Range{lo, hi}, ed);
  }

  if (op == OperatorKind::Delete) ed.clamp_cursor_to_line();
  if (op == OperatorKind::Change) mode_ = Mode::Insert;
  reset_pending();
}

void ModeStateMachine::begin_insert_change(LastChange lc) {
  pending_change_ = std::move(lc);
  insert_session_text_.clear();
}

void ModeStateMachine::replay_insert_text(const std::string& text, Editor& ed) {
  for (char c : text) {
    ed.insert_char(c);
  }
  ed.clamp_cursor_to_line();
}

void ModeStateMachine::repeat_last_change(Editor& ed) {
  const LastChange& lc = last_change_;
  switch (lc.kind) {
    case ChangeKind::None:
      return;
    case ChangeKind::DeleteChar:
      execute_delete_char(lc.count, ed, lc.register_name);
      return;
    case ChangeKind::Paste:
      execute_paste(lc.paste_before, lc.count, ed, lc.register_name);
      return;
    case ChangeKind::LinewiseOperator:
      execute_operator_linewise(lc.op, ed.cursor().line, lc.count, ed, lc.register_name);
      if (lc.op == OperatorKind::Delete) {
        ed.clamp_cursor_to_line();
      } else {
        replay_insert_text(lc.typed_text, ed);
      }
      return;
    case ChangeKind::CharwiseOperator: {
      size_t a = ed.cursor_offset();
      size_t b = a;
      bool inclusive = false;
      if (!resolve_charwise_motion(lc.motion_ch, lc.op, lc.count, ed, b, inclusive)) {
        return;
      }
      size_t lo = std::min(a, b);
      size_t hi = std::max(a, b);
      if (inclusive) hi = std::min(hi + 1, ed.buffer().size());
      execute_operator_charwise(lc.op, Range{lo, hi}, ed, lc.register_name);
      if (lc.op == OperatorKind::Delete) {
        ed.clamp_cursor_to_line();
      } else {
        replay_insert_text(lc.typed_text, ed);
      }
      return;
    }
    case ChangeKind::TextObjectOperator: {
      Range r = text_object_inner_word(ed.buffer(), ed.cursor_offset());
      execute_operator_charwise(lc.op, r, ed, lc.register_name);
      if (lc.op == OperatorKind::Delete) {
        ed.clamp_cursor_to_line();
      } else {
        replay_insert_text(lc.typed_text, ed);
      }
      return;
    }
    case ChangeKind::PlainInsert:
      ed.begin_undo_group();
      switch (lc.insert_trigger) {
        case 'a': {
          Cursor c = ed.cursor();
          size_t len = ed.buffer().line_text(c.line).size();
          if (c.col < len) {
            ++c.col;
            ed.set_cursor(c);
          }
          break;
        }
        case 'o':
          ed.open_line_below();
          break;
        case 'O':
          ed.open_line_above();
          break;
        default:
          break;  // 'i': insert right where the cursor already is
      }
      replay_insert_text(lc.typed_text, ed);
      return;
  }
}

KeyResult ModeStateMachine::handle_normal(KeyEvent ev, Editor& ed) {
  if (ev.key == Key::Escape) {
    reset_pending();
    return KeyResult{};
  }
  if (ev.key == Key::CtrlR) {
    ed.redo();
    reset_pending();
    return KeyResult{};
  }
  if (ev.key == Key::CtrlW) {
    ed.next_window();
    reset_pending();
    return KeyResult{};
  }
  if (ev.key == Key::CtrlP) {
    execute_paste(/*before=*/false, 1, ed, pending_.register_name);
    last_change_ = LastChange{ChangeKind::Paste, OperatorKind::Delete, 0, 0, 1, false,
                               pending_.register_name, {}};
    reset_pending();
    return KeyResult{};
  }
  if (ev.key == Key::CtrlC) {
    // Standalone Ctrl-C (no active Visual selection) copies the current
    // line, matching vim's "yy" -- the closest thing to "copy" you can do
    // without a selection.
    execute_operator_linewise(OperatorKind::Yank, ed.cursor().line, 1, ed,
                               pending_.register_name);
    reset_pending();
    return KeyResult{};
  }

  int repeat = pending_.count > 0 ? pending_.count : 1;

  if (ev.key == Key::Left) {
    apply_plain_motion('h', ed, repeat);
    reset_pending();
    return KeyResult{};
  }
  if (ev.key == Key::Right) {
    apply_plain_motion('l', ed, repeat);
    reset_pending();
    return KeyResult{};
  }
  if (ev.key == Key::Up) {
    apply_plain_motion('k', ed, repeat);
    reset_pending();
    return KeyResult{};
  }
  if (ev.key == Key::Down) {
    apply_plain_motion('j', ed, repeat);
    reset_pending();
    return KeyResult{};
  }
  if (ev.key != Key::Char) {
    return KeyResult{};
  }

  char ch = ev.ch;

  if (pending_.awaiting_register_name) {
    pending_.awaiting_register_name = false;
    if (ch >= 'a' && ch <= 'z') {
      pending_.register_name = ch;
      return KeyResult{};
    }
    reset_pending();
    return KeyResult{};
  }

  if (pending_.awaiting_g_command) {
    pending_.awaiting_g_command = false;
    if (ch == 'd') {
      ed.go_to_definition();
    } else if (ch == 'g') {
      // "gg" / "[count]gg" -- go to the first line, or line [count].
      size_t total = ed.buffer().line_count();
      size_t target = pending_.count > 0
                           ? std::min(static_cast<size_t>(pending_.count) - 1,
                                      total > 0 ? total - 1 : 0)
                           : 0;
      ed.set_cursor(Cursor{target, 0});
      ed.clamp_cursor_to_line();
    }
    reset_pending();
    return KeyResult{};
  }

  if (pending_.op.has_value()) {
    finish_pending_operator(ch, ed);
    return KeyResult{};
  }

  if ((ch >= '1' && ch <= '9') || (ch == '0' && pending_.count > 0)) {
    pending_.count = pending_.count * 10 + (ch - '0');
    return KeyResult{};
  }

  if (!replaying_remap_ && pending_.count == 0) {
    auto it = normal_remap_.find(ch);
    if (it != normal_remap_.end()) {
      std::string rhs = it->second;
      reset_pending();
      replaying_remap_ = true;
      for (char rc : rhs) {
        handle_key(KeyEvent{Key::Char, rc}, ed);
      }
      replaying_remap_ = false;
      return KeyResult{};
    }
  }

  if (apply_plain_motion(ch, ed, repeat)) {
    reset_pending();
    return KeyResult{};
  }

  switch (ch) {
    case 'i':
      ed.begin_undo_group();
      mode_ = Mode::Insert;
      begin_insert_change(LastChange{ChangeKind::PlainInsert, OperatorKind::Delete, 0, 'i', 1, false, 0, {}});
      break;
    case 'a': {
      ed.begin_undo_group();
      Cursor c = ed.cursor();
      size_t len = ed.buffer().line_text(c.line).size();
      if (c.col < len) {
        ++c.col;
        ed.set_cursor(c);
      }
      mode_ = Mode::Insert;
      begin_insert_change(LastChange{ChangeKind::PlainInsert, OperatorKind::Delete, 0, 'a', 1, false, 0, {}});
      break;
    }
    case 'o':
      ed.begin_undo_group();
      ed.open_line_below();
      mode_ = Mode::Insert;
      begin_insert_change(LastChange{ChangeKind::PlainInsert, OperatorKind::Delete, 0, 'o', 1, false, 0, {}});
      break;
    case 'O':
      ed.begin_undo_group();
      ed.open_line_above();
      mode_ = Mode::Insert;
      begin_insert_change(LastChange{ChangeKind::PlainInsert, OperatorKind::Delete, 0, 'O', 1, false, 0, {}});
      break;
    case ':':
      mode_ = Mode::CommandLine;
      command_line_buffer_.clear();
      line_input_prefix_ = ':';
      break;
    case '/':
    case '?':
      mode_ = Mode::Search;
      command_line_buffer_.clear();
      line_input_prefix_ = ch;
      break;
    case 'n':
      ed.jump_to_search(ed.last_search_forward());
      break;
    case 'N':
      ed.jump_to_search(!ed.last_search_forward());
      break;
    case '*':
    case '#': {
      Range word = text_object_inner_word(ed.buffer(), ed.cursor_offset());
      if (word.end > word.start) {
        std::string pattern = ed.buffer().text_range(word.start, word.end - word.start);
        ed.set_last_search(pattern, ch == '*');
        ed.jump_to_search(ch == '*');
      }
      break;
    }
    case '"':
      pending_.awaiting_register_name = true;
      return KeyResult{};
    case 'g':
      pending_.awaiting_g_command = true;
      return KeyResult{};
    case 'K':
      ed.request_hover();
      break;
    case 'd':
      pending_.op = OperatorKind::Delete;
      return KeyResult{};
    case 'y':
      pending_.op = OperatorKind::Yank;
      return KeyResult{};
    case 'c':
      pending_.op = OperatorKind::Change;
      return KeyResult{};
    case 'x':
      execute_delete_char(repeat, ed, pending_.register_name);
      last_change_ = LastChange{ChangeKind::DeleteChar, OperatorKind::Delete, 0, 0, repeat, false,
                                 pending_.register_name, {}};
      break;
    case 'p':
      execute_paste(/*before=*/false, repeat, ed, pending_.register_name);
      last_change_ = LastChange{ChangeKind::Paste, OperatorKind::Delete, 0, 0, repeat, false,
                                 pending_.register_name, {}};
      break;
    case 'P':
      execute_paste(/*before=*/true, repeat, ed, pending_.register_name);
      last_change_ = LastChange{ChangeKind::Paste, OperatorKind::Delete, 0, 0, repeat, true,
                                 pending_.register_name, {}};
      break;
    case '.':
      repeat_last_change(ed);
      break;
    case 'u':
      ed.undo();
      break;
    case 'v':
      enter_visual(Mode::Visual, ed);
      break;
    case 'V':
      enter_visual(Mode::VisualLine, ed);
      break;
    case 'G': {
      // "G" / "[count]G" -- go to the last line, or line [count].
      size_t total = ed.buffer().line_count();
      size_t target = pending_.count > 0
                          ? std::min(static_cast<size_t>(pending_.count) - 1,
                                     total > 0 ? total - 1 : 0)
                          : (total > 0 ? total - 1 : 0);
      ed.set_cursor(Cursor{target, 0});
      ed.clamp_cursor_to_line();
      break;
    }
    default:
      break;
  }
  reset_pending();
  return KeyResult{};
}

KeyResult ModeStateMachine::handle_insert(KeyEvent ev, Editor& ed) {
  if (ev.key == Key::Escape) {
    mode_ = Mode::Normal;
    ed.clamp_cursor_to_line();
    if (pending_change_.has_value()) {
      pending_change_->typed_text = insert_session_text_;
      last_change_ = std::move(*pending_change_);
      pending_change_.reset();
    }
  } else if (ev.key == Key::Enter) {
    ed.insert_char('\n');
    insert_session_text_.push_back('\n');
  } else if (ev.key == Key::Backspace) {
    ed.backspace();
    if (!insert_session_text_.empty()) {
      insert_session_text_.pop_back();
    }
  } else if (ev.key == Key::Left) {
    ed.move_left();
  } else if (ev.key == Key::Right) {
    ed.move_right();
  } else if (ev.key == Key::Up) {
    ed.move_up();
  } else if (ev.key == Key::Down) {
    ed.move_down();
  } else if (ev.key == Key::Char) {
    ed.insert_char(ev.ch);
    insert_session_text_.push_back(ev.ch);
  } else if (ev.key == Key::Tab) {
    for (int i = 0; i < ed.tabstop(); ++i) {
      ed.insert_char(' ');
      insert_session_text_.push_back(' ');
    }
  } else if (ev.key == Key::CtrlP) {
    // Unlike Normal-mode Ctrl-P (which is vim's 'p': linewise paste lands
    // on its own line below), this splices the register's raw text in at
    // the cursor, matching how paste behaves inside a text field in any
    // GUI editor -- you're mid-sentence, not placing a whole line.
    for (char c : ed.unnamed_register().text) {
      ed.insert_char(c);
      insert_session_text_.push_back(c);
    }
  }
  return KeyResult{};
}

KeyResult ModeStateMachine::handle_visual(KeyEvent ev, Editor& ed) {
  if (ev.key == Key::Escape) {
    mode_ = Mode::Normal;
    reset_pending();
    return KeyResult{};
  }
  if (ev.key == Key::Left) {
    apply_plain_motion('h', ed, 1);
    return KeyResult{};
  }
  if (ev.key == Key::Right) {
    apply_plain_motion('l', ed, 1);
    return KeyResult{};
  }
  if (ev.key == Key::Up) {
    apply_plain_motion('k', ed, 1);
    return KeyResult{};
  }
  if (ev.key == Key::Down) {
    apply_plain_motion('j', ed, 1);
    return KeyResult{};
  }
  if (ev.key == Key::CtrlC) {
    // Copy the selection and return to Normal mode, matching vim's own
    // 'y' in Visual mode (yank always exits Visual) and a GUI editor's
    // Ctrl-C (copy never destroys the selection's source text).
    finish_operator_on_visual_selection(OperatorKind::Yank, ed);
    return KeyResult{};
  }
  if (ev.key != Key::Char) {
    return KeyResult{};
  }

  switch (ev.ch) {
    case 'h':
    case 'l':
    case 'j':
    case 'k':
    case 'w':
    case 'b':
    case 'e':
    case '0':
    case '$':
      apply_plain_motion(ev.ch, ed, 1);
      break;
    case 'd':
      finish_operator_on_visual_selection(OperatorKind::Delete, ed);
      break;
    case 'y':
      finish_operator_on_visual_selection(OperatorKind::Yank, ed);
      break;
    case 'c':
      finish_operator_on_visual_selection(OperatorKind::Change, ed);
      break;
    default:
      break;
  }
  return KeyResult{};
}

KeyResult ModeStateMachine::handle_command_line(KeyEvent ev, Editor& ed) {
  if (ev.key == Key::Escape) {
    command_line_buffer_.clear();
    mode_ = Mode::Normal;
    return KeyResult{};
  }
  if (ev.key == Key::Backspace) {
    if (command_line_buffer_.empty()) {
      mode_ = Mode::Normal;
    } else {
      command_line_buffer_.pop_back();
    }
    return KeyResult{};
  }
  if (ev.key == Key::Char) {
    command_line_buffer_.push_back(ev.ch);
    return KeyResult{};
  }
  if (ev.key != Key::Enter) {
    return KeyResult{};
  }

  ExCommand cmd = parse_ex_command(command_line_buffer_);
  command_line_buffer_.clear();
  mode_ = Mode::Normal;
  last_error_.clear();

  switch (cmd.kind) {
    case ExCommandKind::Write:
      try {
        ed.save();
      } catch (const FileIoError& e) {
        last_error_ = e.what();
      }
      break;
    case ExCommandKind::Quit:
      if (!ed.dirty()) {
        ed.request_quit();
      }
      break;
    case ExCommandKind::ForceQuit:
      ed.request_quit();
      break;
    case ExCommandKind::WriteQuit:
      try {
        ed.save();
        ed.request_quit();
      } catch (const FileIoError& e) {
        last_error_ = e.what();
      }
      break;
    case ExCommandKind::Edit:
      if (cmd.argument.empty()) {
        last_error_ = "no file name";
      } else {
        ed.open_buffer(cmd.argument);
      }
      break;
    case ExCommandKind::NextBuffer:
      ed.next_buffer();
      break;
    case ExCommandKind::PrevBuffer:
      ed.prev_buffer();
      break;
    case ExCommandKind::ListBuffers:
      // The buffer list itself is rendered by the frontend (tab bar); this
      // command has nothing further to do at the core layer.
      break;
    case ExCommandKind::SplitHorizontal:
      ed.split_horizontal();
      break;
    case ExCommandKind::SplitVertical:
      ed.split_vertical();
      break;
    case ExCommandKind::CloseWindow:
      ed.close_window();
      break;
    case ExCommandKind::Diff:
      if (cmd.argument.empty()) {
        last_error_ = "no file name";
      } else {
        ed.diff_with(cmd.argument);
      }
      break;
    case ExCommandKind::Lua: {
      // Live scripting hook: runs the same zedit.set_option/map API the
      // startup config uses. Color overrides are parsed too (ConfigResult
      // always carries them) but not applied here -- core has no concept
      // of a theme, only the frontend does, and the startup-only config
      // load is the one place that bridges the two (see main.cpp). That's
      // a deliberate scope cut, not an oversight.
      ConfigResult result = eval_lua(cmd.argument);
      if (result.options.tabstop.has_value()) {
        ed.set_tabstop(*result.options.tabstop);
      }
      ed.set_normal_remap(result.normal_remap);
      if (!result.errors.empty()) {
        last_error_ = result.errors.front();
      }
      break;
    }
    case ExCommandKind::GotoLine: {
      // ":N" -- vim's line-number-only Ex command. 1-indexed like vim;
      // out-of-range values (including a digit string too long for
      // stoul to parse) clamp to the nearest valid line rather than
      // erroring, matching vim's own forgiving behavior here.
      size_t total = ed.buffer().line_count();
      size_t requested = total;
      try {
        requested = std::stoul(cmd.argument);
      } catch (const std::exception&) {
        requested = total;  // absurdly large -- treat like "go to the end"
      }
      size_t target = requested > 0 ? requested - 1 : 0;
      target = std::min(target, total > 0 ? total - 1 : 0);
      ed.set_cursor(Cursor{target, 0});
      ed.clamp_cursor_to_line();
      break;
    }
    case ExCommandKind::Empty:
    case ExCommandKind::Unknown:
      break;
  }

  if (ed.should_quit()) {
    return KeyResult{EditorAction::Quit};
  }
  return KeyResult{};
}

KeyResult ModeStateMachine::handle_search(KeyEvent ev, Editor& ed) {
  if (ev.key == Key::Escape) {
    command_line_buffer_.clear();
    mode_ = Mode::Normal;
    return KeyResult{};
  }
  if (ev.key == Key::Backspace) {
    if (command_line_buffer_.empty()) {
      mode_ = Mode::Normal;
    } else {
      command_line_buffer_.pop_back();
    }
    return KeyResult{};
  }
  if (ev.key == Key::Char) {
    command_line_buffer_.push_back(ev.ch);
    return KeyResult{};
  }
  if (ev.key != Key::Enter) {
    return KeyResult{};
  }

  bool forward = (line_input_prefix_ == '/');
  const std::string& pattern =
      command_line_buffer_.empty() ? ed.last_search_pattern() : command_line_buffer_;
  if (!pattern.empty()) {
    ed.set_last_search(pattern, forward);
    ed.jump_to_search(forward);
  }
  command_line_buffer_.clear();
  mode_ = Mode::Normal;
  return KeyResult{};
}

}  // namespace zedit::core
