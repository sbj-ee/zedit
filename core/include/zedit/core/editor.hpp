#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "zedit/core/cursor.hpp"
#include "zedit/core/mode.hpp"
#include "zedit/core/mode_state_machine.hpp"
#include "zedit/core/piece_table.hpp"
#include "zedit/core/registers.hpp"

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
  Cursor offset_to_cursor(size_t offset) const;

  Mode mode() const { return mode_sm_.mode(); }
  const std::string& command_line_buffer() const {
    return mode_sm_.command_line_buffer();
  }
  const std::string& last_error() const { return mode_sm_.last_error(); }
  Cursor visual_anchor() const { return mode_sm_.visual_anchor(); }

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

  // General-purpose range edits used by operator (d/y/c/x/p) execution.
  void erase_range(size_t offset, size_t length);
  void insert_text(size_t offset, std::string_view text);
  size_t current_line_length() const;

  // The unnamed register, populated by delete/change/yank and consumed by
  // paste. Named registers are a later milestone.
  const RegisterContent& unnamed_register() const { return unnamed_register_; }
  void set_unnamed_register(std::string text, bool linewise) {
    unnamed_register_ = RegisterContent{std::move(text), linewise};
  }

  // Each undo-able user action snapshots buffer + cursor state once via
  // begin_undo_group() before mutating; undo()/redo() restore snapshots.
  // Because PieceTable snapshots are just the piece list (see
  // PieceTable::Snapshot), this is cheap regardless of document size.
  void begin_undo_group();
  void undo();
  void redo();

 private:
  PieceTable buffer_;
  Cursor cursor_;
  ModeStateMachine mode_sm_;
  std::string filename_;
  bool dirty_ = false;
  bool should_quit_ = false;
  RegisterContent unnamed_register_;

  struct UndoEntry {
    PieceTable::Snapshot buffer_snapshot;
    Cursor cursor;
  };
  std::vector<UndoEntry> undo_stack_;
  std::vector<UndoEntry> redo_stack_;

  void mark_dirty() { dirty_ = true; }
};

}  // namespace zedit::core
