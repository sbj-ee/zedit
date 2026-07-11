#include "zedit/core/mode_state_machine.hpp"

#include <algorithm>

#include "zedit/core/command_line.hpp"
#include "zedit/core/editor.hpp"
#include "zedit/core/file_io.hpp"
#include "zedit/core/motion.hpp"
#include "zedit/core/operator.hpp"

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

void ModeStateMachine::finish_pending_operator(char ch, Editor& ed) {
  OperatorKind op = *pending_.op;
  int count = pending_.count > 0 ? pending_.count : 1;

  if (pending_.awaiting_inner_object) {
    if (ch == 'w') {
      Range r = text_object_inner_word(ed.buffer(), ed.cursor_offset());
      execute_operator_charwise(op, r, ed);
      if (op == OperatorKind::Delete) ed.clamp_cursor_to_line();
      if (op == OperatorKind::Change) mode_ = Mode::Insert;
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
    execute_operator_linewise(op, ed.cursor().line, count, ed);
    if (op == OperatorKind::Change) mode_ = Mode::Insert;
    reset_pending();
    return;
  }

  size_t a = ed.cursor_offset();
  size_t b = a;
  bool inclusive = false;
  bool valid_motion = true;

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
      break;
    case 'b':
      for (int i = 0; i < count; ++i) b = motion_word_backward(ed.buffer(), b);
      break;
    case 'e':
      for (int i = 0; i < count; ++i) b = motion_word_end_forward(ed.buffer(), b);
      inclusive = true;
      break;
    case '0':
      b = motion_line_start(ed.buffer(), a);
      break;
    case '$':
      b = motion_line_end(ed.buffer(), a);
      inclusive = true;
      break;
    case 'h':
      for (int i = 0; i < count; ++i) b = offset_left_within_line(ed, b);
      break;
    case 'l':
      for (int i = 0; i < count; ++i) b = offset_right_within_line(ed, b);
      break;
    default:
      valid_motion = false;
      break;
  }

  if (valid_motion) {
    size_t lo = std::min(a, b);
    size_t hi = std::max(a, b);
    if (inclusive) hi = std::min(hi + 1, ed.buffer().size());
    execute_operator_charwise(op, Range{lo, hi}, ed);
    if (op == OperatorKind::Delete) ed.clamp_cursor_to_line();
    if (op == OperatorKind::Change) mode_ = Mode::Insert;
  }
  reset_pending();
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

  if (pending_.op.has_value()) {
    finish_pending_operator(ch, ed);
    return KeyResult{};
  }

  if ((ch >= '1' && ch <= '9') || (ch == '0' && pending_.count > 0)) {
    pending_.count = pending_.count * 10 + (ch - '0');
    return KeyResult{};
  }

  if (apply_plain_motion(ch, ed, repeat)) {
    reset_pending();
    return KeyResult{};
  }

  switch (ch) {
    case 'i':
      ed.begin_undo_group();
      mode_ = Mode::Insert;
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
      break;
    }
    case 'o':
      ed.begin_undo_group();
      ed.open_line_below();
      mode_ = Mode::Insert;
      break;
    case 'O':
      ed.begin_undo_group();
      ed.open_line_above();
      mode_ = Mode::Insert;
      break;
    case ':':
      mode_ = Mode::CommandLine;
      command_line_buffer_.clear();
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
      execute_delete_char(repeat, ed);
      break;
    case 'p':
      execute_paste(/*before=*/false, repeat, ed);
      break;
    case 'P':
      execute_paste(/*before=*/true, repeat, ed);
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
  } else if (ev.key == Key::Enter) {
    ed.insert_char('\n');
  } else if (ev.key == Key::Backspace) {
    ed.backspace();
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
    case ExCommandKind::Empty:
    case ExCommandKind::Unknown:
      break;
  }

  if (ed.should_quit()) {
    return KeyResult{EditorAction::Quit};
  }
  return KeyResult{};
}

}  // namespace zedit::core
