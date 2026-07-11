#pragma once

#include <string>

#include "zedit/core/cursor.hpp"
#include "zedit/core/mode.hpp"
#include "zedit/core/mode_state_machine.hpp"
#include "zedit/core/piece_table.hpp"

namespace zedit::core {

// Facade tying the buffer, cursor, and modal state machine together. This is
// the API surface both the GUI frontend and headless unit tests drive.
class Editor {
 public:
  Editor() = default;
  static Editor open_file(const std::string& path);

  KeyResult handle_key(KeyEvent ev) { return mode_sm_.handle_key(ev, *this); }

  PieceTable& buffer() { return buffer_; }
  const PieceTable& buffer() const { return buffer_; }

  Cursor cursor() const { return cursor_; }
  void set_cursor(Cursor c) { cursor_ = c; }
  size_t cursor_offset() const;

  Mode mode() const { return mode_sm_.mode(); }
  const std::string& command_line_buffer() const {
    return mode_sm_.command_line_buffer();
  }
  const std::string& last_error() const { return mode_sm_.last_error(); }

  const std::string& filename() const { return filename_; }
  void set_filename(std::string path) { filename_ = std::move(path); }

  bool dirty() const { return dirty_; }
  bool should_quit() const { return should_quit_; }
  void request_quit() { should_quit_ = true; }

  void save();
  void save_as(const std::string& path);

  // Cursor motion, clamped per the current mode's valid column range
  // (Normal mode sits on a character; Insert mode may sit one past the
  // last character).
  void move_left();
  void move_right();
  void move_up();
  void move_down();
  void clamp_cursor_to_line();

  // Editing operations used by the mode state machine's Insert handling.
  void insert_char(char c);
  void backspace();
  void open_line_below();
  void open_line_above();

 private:
  PieceTable buffer_;
  Cursor cursor_;
  ModeStateMachine mode_sm_;
  std::string filename_;
  bool dirty_ = false;
  bool should_quit_ = false;

  size_t current_line_length() const;
  void mark_dirty() { dirty_ = true; }
};

}  // namespace zedit::core
