#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "zedit/core/cursor.hpp"
#include "zedit/core/highlight.hpp"
#include "zedit/core/mode.hpp"
#include "zedit/core/mode_state_machine.hpp"
#include "zedit/core/piece_table.hpp"
#include "zedit/core/registers.hpp"

namespace zedit::core {

// Facade tying the buffer, cursor, and modal state machine together. This is
// the API surface both the GUI frontend and headless unit tests drive.
//
// Editor can hold multiple open buffers (:e, :bn, :bp); every method below
// that touches "the buffer" -- buffer(), cursor(), filename(), dirty(),
// save(), undo()/redo(), etc. -- operates on whichever one is current, so
// existing callers (ModeStateMachine, the operator functions, the frontend)
// needed no changes when multi-buffer support was added. Registers, search
// state, and the modal state machine are shared across buffers, matching
// vim: switching buffers doesn't clear your registers or last search.
class Editor {
 public:
  Editor() = default;
  static Editor open_file(const std::string& path);

  KeyResult handle_key(KeyEvent ev) { return mode_sm_.handle_key(ev, *this); }

  PieceTable& buffer() { return cur().content; }
  const PieceTable& buffer() const { return cur().content; }

  Cursor cursor() const { return cur().cursor; }
  void set_cursor(Cursor c) { cur().cursor = c; }
  size_t cursor_offset() const;
  Cursor offset_to_cursor(size_t offset) const;

  Mode mode() const { return mode_sm_.mode(); }
  const std::string& command_line_buffer() const {
    return mode_sm_.command_line_buffer();
  }
  char line_input_prefix() const { return mode_sm_.line_input_prefix(); }
  const std::string& last_error() const { return mode_sm_.last_error(); }
  Cursor visual_anchor() const { return mode_sm_.visual_anchor(); }

  const std::string& filename() const { return cur().filename; }
  // Also re-picks the syntax highlighter to match the new filename's
  // extension (e.g. starting an empty buffer destined for "foo.cpp").
  void set_filename(std::string path);

  bool dirty() const { return cur().dirty; }
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

  // Registers populated by delete/change/yank and consumed by paste. `name`
  // 0 means the unnamed register (the default target). A named register
  // ('a'-'z') also updates the unnamed register, matching vim: "ayy then a
  // bare p pastes what you just yanked, register prefix or not.
  const RegisterContent& unnamed_register() const { return unnamed_register_; }
  void set_unnamed_register(std::string text, bool linewise) {
    unnamed_register_ = RegisterContent{std::move(text), linewise};
  }
  const RegisterContent& register_content(char name) const {
    if (name == 0) return unnamed_register_;
    return named_registers_[static_cast<size_t>(name - 'a')];
  }
  void set_register(char name, std::string text, bool linewise) {
    if (name == 0) {
      set_unnamed_register(std::move(text), linewise);
      return;
    }
    unnamed_register_ = RegisterContent{text, linewise};
    named_registers_[static_cast<size_t>(name - 'a')] =
        RegisterContent{std::move(text), linewise};
  }

  // Each undo-able user action snapshots buffer + cursor state once via
  // begin_undo_group() before mutating; undo()/redo() restore snapshots.
  // Because PieceTable snapshots are just the piece list (see
  // PieceTable::Snapshot), this is cheap regardless of document size. Undo
  // history is per-buffer, like vim.
  void begin_undo_group();
  void undo();
  void redo();

  // Search state persists across mode transitions and buffer switches (like
  // registers) so 'n' and 'N' can repeat the last '/' or '?' search, or the
  // last '*'/'#' word search, regardless of what's happened since.
  const std::string& last_search_pattern() const { return last_search_pattern_; }
  bool last_search_forward() const { return last_search_forward_; }
  void set_last_search(std::string pattern, bool forward) {
    last_search_pattern_ = std::move(pattern);
    last_search_forward_ = forward;
  }
  // Jumps to the next/previous match of the last search pattern. Returns
  // false (no-op) if there is no pattern set or it doesn't occur anywhere.
  bool jump_to_search(bool forward);

  // Multiple buffers (:e, :bn, :bp, :ls). Opening a path that's already
  // open just switches to it rather than duplicating it; opening a path
  // that doesn't exist on disk yet starts an empty buffer with that name,
  // matching vim's ":e newfile.txt".
  size_t buffer_count() const { return buffers_.size(); }
  size_t current_buffer_index() const { return current_; }
  const std::string& buffer_filename(size_t index) const {
    return buffers_[index].filename;
  }
  bool buffer_dirty(size_t index) const { return buffers_[index].dirty; }
  void switch_to_buffer(size_t index);
  void next_buffer();
  void prev_buffer();
  void open_buffer(const std::string& path);

  // The current buffer's syntax highlighter (one per buffer, since
  // different open files can be different languages). Defaults to a
  // PlainHighlighter; callers that know the file type (e.g. Editor's own
  // open_file/open_buffer, by extension) can install a real one.
  Highlighter& highlighter() { return *cur().highlighter; }
  const Highlighter& highlighter() const { return *cur().highlighter; }
  void set_highlighter(std::unique_ptr<Highlighter> h) {
    cur().highlighter = std::move(h);
    cur().highlighter->set_text(buffer().to_string());
  }

 private:
  struct UndoEntry {
    PieceTable::Snapshot buffer_snapshot;
    Cursor cursor;
  };

  struct Buffer {
    PieceTable content;
    Cursor cursor;
    std::string filename;
    bool dirty = false;
    std::vector<UndoEntry> undo_stack;
    std::vector<UndoEntry> redo_stack;
    std::unique_ptr<Highlighter> highlighter = std::make_unique<PlainHighlighter>();
  };

  std::vector<Buffer> buffers_ = std::vector<Buffer>(1);
  size_t current_ = 0;
  ModeStateMachine mode_sm_;
  bool should_quit_ = false;
  RegisterContent unnamed_register_;
  std::array<RegisterContent, 26> named_registers_;
  std::string last_search_pattern_;
  bool last_search_forward_ = true;

  Buffer& cur() { return buffers_[current_]; }
  const Buffer& cur() const { return buffers_[current_]; }
  void mark_dirty() {
    cur().dirty = true;
    cur().highlighter->set_text(cur().content.to_string());
  }
};

}  // namespace zedit::core
