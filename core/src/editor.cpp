#include "zedit/core/editor.hpp"

#include <algorithm>
#include <utility>

#include "zedit/core/file_io.hpp"
#include "zedit/core/languages.hpp"
#include "zedit/core/search.hpp"

namespace zedit::core {

namespace {

std::string leading_whitespace(std::string_view line) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    ++i;
  }
  return std::string(line.substr(0, i));
}

}  // namespace

Editor Editor::open_file(const std::string& path) {
  Editor ed;
  ed.cur_buffer().content = PieceTable(read_file(path));
  ed.set_filename(path);
  return ed;
}

void Editor::set_filename(std::string path) {
  cur_buffer().filename = std::move(path);
  set_highlighter(make_highlighter_for_filename(cur_buffer().filename));
  maybe_start_lsp_for_current_buffer();
}

void Editor::maybe_start_lsp_for_current_buffer() {
  if (!is_cpp_filename(cur_buffer().filename)) {
    return;
  }
  if (!lsp_->running()) {
    if (!lsp_->start("clangd")) {
      return;  // clangd not installed -- no LSP features, not an error
    }
  }
  lsp_->open_document(cur_buffer().filename, cur_buffer().content.to_string());
}

void Editor::go_to_definition() {
  if (!lsp_->running()) {
    return;
  }
  lsp_->request_definition(filename(), cursor().line, cursor().col,
                            [this](std::optional<LspLocation> loc) {
                              if (!loc) return;
                              open_buffer(loc->path);
                              set_cursor(Cursor{loc->line, loc->col});
                              clamp_cursor_to_line();
                            });
}

void Editor::request_hover() {
  if (!lsp_->running()) {
    return;
  }
  hover_text_.reset();
  lsp_->request_hover(filename(), cursor().line, cursor().col,
                       [this](std::optional<std::string> text) { hover_text_ = std::move(text); });
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
  cur_window().cursor.col = std::min(cur_window().cursor.col, max_col);
}

void Editor::move_left() {
  if (cur_window().cursor.col > 0) {
    --cur_window().cursor.col;
  }
}

void Editor::move_right() {
  size_t len = current_line_length();
  size_t max_col = (mode() == Mode::Insert) ? len : (len > 0 ? len - 1 : 0);
  if (cur_window().cursor.col < max_col) {
    ++cur_window().cursor.col;
  }
}

void Editor::move_up() {
  if (cur_window().cursor.line > 0) {
    --cur_window().cursor.line;
  }
  clamp_cursor_to_line();
}

void Editor::move_down() {
  if (cur_window().cursor.line + 1 < buffer().line_count()) {
    ++cur_window().cursor.line;
  }
  clamp_cursor_to_line();
}

void Editor::insert_char(char c) {
  size_t offset = cursor_offset();
  if (c == '\n') {
    buffer().insert(offset, "\n");
    ++cur_window().cursor.line;
    cur_window().cursor.col = 0;
  } else {
    buffer().insert(offset, std::string_view(&c, 1));
    ++cur_window().cursor.col;
  }
  mark_dirty();
}

void Editor::backspace() {
  if (cur_window().cursor.col > 0) {
    size_t offset = cursor_offset();
    buffer().erase(offset - 1, 1);
    --cur_window().cursor.col;
    mark_dirty();
  } else if (cur_window().cursor.line > 0) {
    size_t prev_line = cur_window().cursor.line - 1;
    size_t prev_len = buffer().line_text(prev_line).size();
    size_t newline_offset = buffer().line_start_offset(cur_window().cursor.line) - 1;
    buffer().erase(newline_offset, 1);
    cur_window().cursor.line = prev_line;
    cur_window().cursor.col = prev_len;
    mark_dirty();
  }
}

void Editor::open_line_below() {
  std::string indent = leading_whitespace(buffer().line_text(cur_window().cursor.line));
  size_t offset = buffer().line_start_offset(cur_window().cursor.line) + current_line_length();
  buffer().insert(offset, "\n" + indent);
  ++cur_window().cursor.line;
  cur_window().cursor.col = indent.size();
  mark_dirty();
}

void Editor::open_line_above() {
  std::string indent = leading_whitespace(buffer().line_text(cur_window().cursor.line));
  size_t offset = buffer().line_start_offset(cur_window().cursor.line);
  buffer().insert(offset, indent + "\n");
  cur_window().cursor.col = indent.size();
  mark_dirty();
}

std::string Editor::leading_whitespace_of_line(size_t line) const {
  return leading_whitespace(buffer().line_text(line));
}

void Editor::save() {
  if (filename().empty()) {
    throw FileIoError("no filename set");
  }
  write_file(filename(), buffer().to_string());
  cur_buffer().dirty = false;
}

void Editor::save_as(const std::string& path) {
  set_filename(path);
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

bool Editor::replace_current_match(std::string_view find, std::string_view replace_with) {
  if (find.empty()) {
    return false;
  }
  size_t offset = cursor_offset();
  if (offset + find.size() > buffer().size()) {
    return false;
  }
  if (buffer().text_range(offset, find.size()) != find) {
    return false;
  }
  begin_undo_group();
  erase_range(offset, find.size());
  insert_text(offset, replace_with);
  set_cursor(offset_to_cursor(offset + replace_with.size()));
  clamp_cursor_to_line();
  return true;
}

size_t Editor::replace_all(std::string_view find, std::string_view replace_with) {
  if (find.empty()) {
    return 0;
  }
  std::string text = buffer().to_string();
  std::vector<size_t> offsets;
  size_t pos = 0;
  while ((pos = text.find(find, pos)) != std::string::npos) {
    offsets.push_back(pos);
    pos += find.size();
  }
  if (offsets.empty()) {
    return 0;
  }
  begin_undo_group();
  // Back-to-front so earlier (not-yet-processed) offsets stay valid --
  // an edit at a later offset never shifts anything before it.
  for (auto it = offsets.rbegin(); it != offsets.rend(); ++it) {
    erase_range(*it, find.size());
    insert_text(*it, replace_with);
  }
  set_cursor(offset_to_cursor(offsets.front()));
  clamp_cursor_to_line();
  return offsets.size();
}

void Editor::sort_lines(size_t start_line, size_t end_line, bool reverse) {
  size_t total = buffer().line_count();
  if (total == 0 || start_line > end_line) {
    return;
  }
  end_line = std::min(end_line, total - 1);

  std::vector<std::string> lines;
  lines.reserve(end_line - start_line + 1);
  for (size_t l = start_line; l <= end_line; ++l) {
    lines.push_back(buffer().line_text(l));
  }
  std::stable_sort(lines.begin(), lines.end());
  if (reverse) {
    std::reverse(lines.begin(), lines.end());
  }

  // Sorting never changes how many lines there are, only their order, so
  // (unlike a linewise delete) there's no risk of leaving a dangling
  // blank line behind -- the trailing-newline convention just needs to
  // match whether this range runs through the buffer's actual end.
  bool through_end = (end_line == total - 1);
  std::string replacement;
  for (size_t i = 0; i < lines.size(); ++i) {
    replacement += lines[i];
    if (i + 1 < lines.size() || !through_end) {
      replacement += '\n';
    }
  }

  size_t erase_start = buffer().line_start_offset(start_line);
  size_t erase_end = through_end ? buffer().size() : buffer().line_start_offset(end_line + 1);

  begin_undo_group();
  erase_range(erase_start, erase_end - erase_start);
  insert_text(erase_start, replacement);
  set_cursor(Cursor{start_line, 0});
  clamp_cursor_to_line();
}

void Editor::begin_undo_group() {
  cur_buffer().undo_stack.push_back(UndoEntry{buffer().snapshot(), cursor()});
  cur_buffer().redo_stack.clear();
}

void Editor::undo() {
  if (cur_buffer().undo_stack.empty()) {
    return;
  }
  cur_buffer().redo_stack.push_back(UndoEntry{buffer().snapshot(), cursor()});
  UndoEntry entry = std::move(cur_buffer().undo_stack.back());
  cur_buffer().undo_stack.pop_back();
  buffer().restore(std::move(entry.buffer_snapshot));
  cur_window().cursor = entry.cursor;
  mark_dirty();
}

void Editor::redo() {
  if (cur_buffer().redo_stack.empty()) {
    return;
  }
  cur_buffer().undo_stack.push_back(UndoEntry{buffer().snapshot(), cursor()});
  UndoEntry entry = std::move(cur_buffer().redo_stack.back());
  cur_buffer().redo_stack.pop_back();
  buffer().restore(std::move(entry.buffer_snapshot));
  cur_window().cursor = entry.cursor;
  mark_dirty();
}

void Editor::switch_window_to_buffer(size_t index) {
  cur_buffer().last_cursor = cur_window().cursor;
  cur_window().buffer_index = index;

  Cursor restored = buffers_[index].last_cursor;
  size_t line_count = buffers_[index].content.line_count();
  size_t max_line = line_count > 0 ? line_count - 1 : 0;
  restored.line = std::min(restored.line, max_line);
  cur_window().cursor = restored;
  clamp_cursor_to_line();
}

void Editor::switch_to_buffer(size_t index) {
  if (index < buffers_.size()) {
    switch_window_to_buffer(index);
  }
}

void Editor::next_buffer() {
  if (!buffers_.empty()) {
    switch_window_to_buffer((cur_window().buffer_index + 1) % buffers_.size());
  }
}

void Editor::prev_buffer() {
  if (!buffers_.empty()) {
    switch_window_to_buffer((cur_window().buffer_index + buffers_.size() - 1) % buffers_.size());
  }
}

void Editor::open_buffer(const std::string& path) {
  for (size_t i = 0; i < buffers_.size(); ++i) {
    if (buffers_[i].filename == path) {
      switch_window_to_buffer(i);
      return;
    }
  }

  Buffer buf;
  try {
    buf.content = PieceTable(read_file(path));
  } catch (const FileTooLargeError&) {
    // Propagate to the caller rather than falling through to the "new
    // file" handling below: silently substituting an empty buffer here
    // would still attach the real file's name to it, so a later save
    // would overwrite the original (huge) file with nothing.
    throw;
  } catch (const FileIoError&) {
    // New file: start empty, same as vim's ":e newfile.txt".
  }
  buffers_.push_back(std::move(buf));
  switch_window_to_buffer(buffers_.size() - 1);
  set_filename(path);
}

void Editor::do_split(SplitLayout requested) {
  if (split_layout_ == SplitLayout::Single) {
    split_layout_ = requested;
  }
  Window new_window = windows_[current_window_];
  windows_.insert(windows_.begin() + static_cast<std::ptrdiff_t>(current_window_) + 1,
                   new_window);
  ++current_window_;
}

void Editor::split_horizontal() { do_split(SplitLayout::Stacked); }
void Editor::split_vertical() { do_split(SplitLayout::SideBySide); }

void Editor::next_window() {
  if (!windows_.empty()) {
    current_window_ = (current_window_ + 1) % windows_.size();
  }
}

void Editor::close_window() {
  if (windows_.size() <= 1) {
    return;
  }
  windows_.erase(windows_.begin() + static_cast<std::ptrdiff_t>(current_window_));
  if (current_window_ >= windows_.size()) {
    current_window_ = windows_.size() - 1;
  }
  if (windows_.size() == 1) {
    split_layout_ = SplitLayout::Single;
  }
}

namespace {
std::vector<std::string> buffer_lines(const PieceTable& content) {
  std::vector<std::string> lines;
  size_t count = content.line_count();
  lines.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    lines.push_back(content.line_text(i));
  }
  return lines;
}
}  // namespace

void Editor::diff_with(const std::string& path) {
  if (diff_pair_ && windows_.size() == 2) {
    // Already comparing with exactly one other pane -- swap what's
    // shown in that pane for the newly picked file instead of splitting
    // again. Without this, calling diff_with() a second time stacked a
    // third window while diff_status_for_window() (which only knows
    // about the latest pair) silently stopped coloring the first pane,
    // since its buffer no longer matched either side of the new pair.
    size_t original_buffer = cur_window().buffer_index;
    size_t other_window = (current_window_ == 0) ? 1 : 0;
    set_current_window(other_window);
    open_buffer(path);
    size_t other_buffer = cur_window().buffer_index;
    diff_pair_ = DiffPair{original_buffer, other_buffer};
    return;
  }
  size_t original_buffer = cur_window().buffer_index;
  split_vertical();
  open_buffer(path);
  size_t other_buffer = cur_window().buffer_index;
  diff_pair_ = DiffPair{original_buffer, other_buffer};
}

std::vector<DiffLineStatus> Editor::diff_status_for_window(size_t window_index) const {
  if (!diff_pair_ || window_index >= windows_.size()) {
    return {};
  }
  size_t buf_index = windows_[window_index].buffer_index;
  if (buf_index != diff_pair_->buffer_a && buf_index != diff_pair_->buffer_b) {
    return {};
  }

  DiffResult result = diff_lines(buffer_lines(buffers_[diff_pair_->buffer_a].content),
                                  buffer_lines(buffers_[diff_pair_->buffer_b].content));
  return (buf_index == diff_pair_->buffer_a) ? result.left : result.right;
}

}  // namespace zedit::core
