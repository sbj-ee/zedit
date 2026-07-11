#include "zedit/core/editor.hpp"

#include <algorithm>
#include <utility>

#include "zedit/core/file_io.hpp"
#include "zedit/core/search.hpp"

namespace zedit::core {

Editor Editor::open_file(const std::string& path) {
  Editor ed;
  ed.cur().filename = path;
  ed.cur().content = PieceTable(read_file(path));
  return ed;
}

size_t Editor::cursor_offset() const {
  return buffer().line_col_to_offset(cursor().line, cursor().col);
}

Cursor Editor::offset_to_cursor(size_t offset) const {
  PieceTable::LineCol lc = buffer().offset_to_line_col(offset);
  return Cursor{lc.line, lc.col};
}

size_t Editor::current_line_length() const {
  return buffer().line_text(cursor().line).size();
}

void Editor::clamp_cursor_to_line() {
  size_t len = current_line_length();
  size_t max_col = (mode() == Mode::Insert) ? len : (len > 0 ? len - 1 : 0);
  cur().cursor.col = std::min(cur().cursor.col, max_col);
}

void Editor::move_left() {
  if (cur().cursor.col > 0) {
    --cur().cursor.col;
  }
}

void Editor::move_right() {
  size_t len = current_line_length();
  size_t max_col = (mode() == Mode::Insert) ? len : (len > 0 ? len - 1 : 0);
  if (cur().cursor.col < max_col) {
    ++cur().cursor.col;
  }
}

void Editor::move_up() {
  if (cur().cursor.line > 0) {
    --cur().cursor.line;
  }
  clamp_cursor_to_line();
}

void Editor::move_down() {
  if (cur().cursor.line + 1 < buffer().line_count()) {
    ++cur().cursor.line;
  }
  clamp_cursor_to_line();
}

void Editor::insert_char(char c) {
  size_t offset = cursor_offset();
  if (c == '\n') {
    buffer().insert(offset, "\n");
    ++cur().cursor.line;
    cur().cursor.col = 0;
  } else {
    buffer().insert(offset, std::string_view(&c, 1));
    ++cur().cursor.col;
  }
  mark_dirty();
}

void Editor::backspace() {
  if (cur().cursor.col > 0) {
    size_t offset = cursor_offset();
    buffer().erase(offset - 1, 1);
    --cur().cursor.col;
    mark_dirty();
  } else if (cur().cursor.line > 0) {
    size_t prev_line = cur().cursor.line - 1;
    size_t prev_len = buffer().line_text(prev_line).size();
    size_t newline_offset = buffer().line_start_offset(cur().cursor.line) - 1;
    buffer().erase(newline_offset, 1);
    cur().cursor.line = prev_line;
    cur().cursor.col = prev_len;
    mark_dirty();
  }
}

void Editor::open_line_below() {
  size_t offset = buffer().line_start_offset(cur().cursor.line) + current_line_length();
  buffer().insert(offset, "\n");
  ++cur().cursor.line;
  cur().cursor.col = 0;
  mark_dirty();
}

void Editor::open_line_above() {
  size_t offset = buffer().line_start_offset(cur().cursor.line);
  buffer().insert(offset, "\n");
  cur().cursor.col = 0;
  mark_dirty();
}

void Editor::save() {
  if (filename().empty()) {
    throw FileIoError("no filename set");
  }
  write_file(filename(), buffer().to_string());
  cur().dirty = false;
}

void Editor::save_as(const std::string& path) {
  cur().filename = path;
  save();
}

void Editor::erase_range(size_t offset, size_t length) {
  if (length == 0) {
    return;
  }
  buffer().erase(offset, length);
  mark_dirty();
}

void Editor::insert_text(size_t offset, std::string_view text) {
  if (text.empty()) {
    return;
  }
  buffer().insert(offset, text);
  mark_dirty();
}

bool Editor::jump_to_search(bool forward) {
  if (last_search_pattern_.empty()) {
    return false;
  }
  std::optional<size_t> pos = forward
                                   ? search_forward(buffer(), cursor_offset(), last_search_pattern_)
                                   : search_backward(buffer(), cursor_offset(), last_search_pattern_);
  if (!pos) {
    return false;
  }
  set_cursor(offset_to_cursor(*pos));
  return true;
}

void Editor::begin_undo_group() {
  cur().undo_stack.push_back(UndoEntry{buffer().snapshot(), cursor()});
  cur().redo_stack.clear();
}

void Editor::undo() {
  if (cur().undo_stack.empty()) {
    return;
  }
  cur().redo_stack.push_back(UndoEntry{buffer().snapshot(), cursor()});
  UndoEntry entry = std::move(cur().undo_stack.back());
  cur().undo_stack.pop_back();
  buffer().restore(std::move(entry.buffer_snapshot));
  cur().cursor = entry.cursor;
  mark_dirty();
}

void Editor::redo() {
  if (cur().redo_stack.empty()) {
    return;
  }
  cur().undo_stack.push_back(UndoEntry{buffer().snapshot(), cursor()});
  UndoEntry entry = std::move(cur().redo_stack.back());
  cur().redo_stack.pop_back();
  buffer().restore(std::move(entry.buffer_snapshot));
  cur().cursor = entry.cursor;
  mark_dirty();
}

void Editor::switch_to_buffer(size_t index) {
  if (index < buffers_.size()) {
    current_ = index;
  }
}

void Editor::next_buffer() {
  if (!buffers_.empty()) {
    current_ = (current_ + 1) % buffers_.size();
  }
}

void Editor::prev_buffer() {
  if (!buffers_.empty()) {
    current_ = (current_ + buffers_.size() - 1) % buffers_.size();
  }
}

void Editor::open_buffer(const std::string& path) {
  for (size_t i = 0; i < buffers_.size(); ++i) {
    if (buffers_[i].filename == path) {
      current_ = i;
      return;
    }
  }

  Buffer buf;
  buf.filename = path;
  try {
    buf.content = PieceTable(read_file(path));
  } catch (const FileIoError&) {
    // New file: start empty, same as vim's ":e newfile.txt".
  }
  buffers_.push_back(std::move(buf));
  current_ = buffers_.size() - 1;
}

}  // namespace zedit::core
