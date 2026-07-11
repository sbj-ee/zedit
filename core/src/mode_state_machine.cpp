#include "zedit/core/mode_state_machine.hpp"

#include "zedit/core/command_line.hpp"
#include "zedit/core/editor.hpp"
#include "zedit/core/file_io.hpp"

namespace zedit::core {

KeyResult ModeStateMachine::handle_key(KeyEvent ev, Editor& ed) {
  switch (mode_) {
    case Mode::Normal:
      return handle_normal(ev, ed);
    case Mode::Insert:
      return handle_insert(ev, ed);
    case Mode::CommandLine:
      return handle_command_line(ev, ed);
  }
  return KeyResult{};
}

KeyResult ModeStateMachine::handle_normal(KeyEvent ev, Editor& ed) {
  if (ev.key == Key::Left) {
    ed.move_left();
  } else if (ev.key == Key::Right) {
    ed.move_right();
  } else if (ev.key == Key::Up) {
    ed.move_up();
  } else if (ev.key == Key::Down) {
    ed.move_down();
  } else if (ev.key == Key::Char) {
    switch (ev.ch) {
      case 'h':
        ed.move_left();
        break;
      case 'l':
        ed.move_right();
        break;
      case 'k':
        ed.move_up();
        break;
      case 'j':
        ed.move_down();
        break;
      case 'i':
        mode_ = Mode::Insert;
        break;
      case 'a': {
        // Unlike Normal-mode 'l', 'a' is allowed to land one past the last
        // character, since that's where the appended text will go.
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
        ed.open_line_below();
        mode_ = Mode::Insert;
        break;
      case 'O':
        ed.open_line_above();
        mode_ = Mode::Insert;
        break;
      case ':':
        mode_ = Mode::CommandLine;
        command_line_buffer_.clear();
        break;
      default:
        break;
    }
  }
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
