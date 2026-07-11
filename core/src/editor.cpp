#include "zedit/core/editor.hpp"

#include <algorithm>
#include <utility>

#include "zedit/core/file_io.hpp"

namespace zedit::core {

Editor Editor::open_file(const std::string& path) {
  Editor ed;
  ed.filename_ = path;
  ed.buffer_ = PieceTable(read_file(path));
  return ed;
}

size_t Editor::cursor_offset() const {
  return buffer_.line_col_to_offset(cursor_.line, cursor_.col);
}

Cursor Editor::offset_to_cursor(size_t offset) const {
  PieceTable::LineCol lc = buffer_.offset_to_line_col(offset);
  return Cursor{lc.line, lc.col};
}

size_t Editor::current_line_length() const {
  return buffer_.line_text(cursor_.line).size();
}

void Editor::clamp_cursor_to_line() {
  size_t len = current_line_length();
  size_t max_col = (mode() == Mode::Insert) ? len : (len > 0 ? len - 1 : 0);
  cursor_.col = std::min(cursor_.col, max_col);
}

void Editor::move_left() {
  if (cursor_.col > 0) {
    --cursor_.col;
  }
}

void Editor::move_right() {
  size_t len = current_line_length();
  size_t max_col = (mode() == Mode::Insert) ? len : (len > 0 ? len - 1 : 0);
  if (cursor_.col < max_col) {
    ++cursor_.col;
  }
}

void Editor::move_up() {
  if (cursor_.line > 0) {
    --cursor_.line;
  }
  clamp_cursor_to_line();
}

void Editor::move_down() {
  if (cursor_.line + 1 < buffer_.line_count()) {
    ++cursor_.line;
  }
  clamp_cursor_to_line();
}

void Editor::insert_char(char c) {
  size_t offset = cursor_offset();
  if (c == '\n') {
    buffer_.insert(offset, "\n");
    ++cursor_.line;
    cursor_.col = 0;
  } else {
    buffer_.insert(offset, std::string_view(&c, 1));
    ++cursor_.col;
  }
  mark_dirty();
}

void Editor::backspace() {
  if (cursor_.col > 0) {
    size_t offset = cursor_offset();
    buffer_.erase(offset - 1, 1);
    --cursor_.col;
    mark_dirty();
  } else if (cursor_.line > 0) {
    size_t prev_line = cursor_.line - 1;
    size_t prev_len = buffer_.line_text(prev_line).size();
    size_t newline_offset = buffer_.line_start_offset(cursor_.line) - 1;
    buffer_.erase(newline_offset, 1);
    cursor_.line = prev_line;
    cursor_.col = prev_len;
    mark_dirty();
  }
}

void Editor::open_line_below() {
  size_t offset = buffer_.line_start_offset(cursor_.line) + current_line_length();
  buffer_.insert(offset, "\n");
  ++cursor_.line;
  cursor_.col = 0;
  mark_dirty();
}

void Editor::open_line_above() {
  size_t offset = buffer_.line_start_offset(cursor_.line);
  buffer_.insert(offset, "\n");
  cursor_.col = 0;
  mark_dirty();
}

void Editor::save() {
  if (filename_.empty()) {
    throw FileIoError("no filename set");
  }
  write_file(filename_, buffer_.to_string());
  dirty_ = false;
}

void Editor::save_as(const std::string& path) {
  filename_ = path;
  save();
}

void Editor::erase_range(size_t offset, size_t length) {
  if (length == 0) {
    return;
  }
  buffer_.erase(offset, length);
  mark_dirty();
}

void Editor::insert_text(size_t offset, std::string_view text) {
  if (text.empty()) {
    return;
  }
  buffer_.insert(offset, text);
  mark_dirty();
}

void Editor::begin_undo_group() {
  undo_stack_.push_back(UndoEntry{buffer_.snapshot(), cursor_});
  redo_stack_.clear();
}

void Editor::undo() {
  if (undo_stack_.empty()) {
    return;
  }
  redo_stack_.push_back(UndoEntry{buffer_.snapshot(), cursor_});
  UndoEntry entry = std::move(undo_stack_.back());
  undo_stack_.pop_back();
  buffer_.restore(std::move(entry.buffer_snapshot));
  cursor_ = entry.cursor;
  mark_dirty();
}

void Editor::redo() {
  if (redo_stack_.empty()) {
    return;
  }
  undo_stack_.push_back(UndoEntry{buffer_.snapshot(), cursor_});
  UndoEntry entry = std::move(redo_stack_.back());
  redo_stack_.pop_back();
  buffer_.restore(std::move(entry.buffer_snapshot));
  cursor_ = entry.cursor;
  mark_dirty();
}

}  // namespace zedit::core
