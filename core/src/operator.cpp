#include "zedit/core/operator.hpp"

#include <algorithm>
#include <string>

namespace zedit::core {

namespace {

// [start_line, end_line) -- the lines dd/yy/cc span, clamped to the buffer.
struct LinewiseSpan {
  size_t start_line;
  size_t end_line;
};

LinewiseSpan linewise_span(const PieceTable& buf, size_t start_line, int count) {
  size_t total = buf.line_count();
  size_t n = static_cast<size_t>(std::max(count, 1));
  return LinewiseSpan{start_line, std::min(start_line + n, total)};
}

// Built line-by-line (not as a raw byte slice) so the result always ends in
// '\n' regardless of whether the file's last line has a trailing newline.
std::string linewise_text(const PieceTable& buf, LinewiseSpan span) {
  std::string text;
  for (size_t line = span.start_line; line < span.end_line; ++line) {
    text += buf.line_text(line);
    text += '\n';
  }
  return text;
}

// The byte range to erase for a linewise delete/change. When the span runs
// through the last line (which has no trailing newline), the range backs up
// to also consume the newline before it, so no dangling blank line is left
// behind.
Range linewise_erase_range(const PieceTable& buf, LinewiseSpan span) {
  size_t total = buf.line_count();
  size_t start = buf.line_start_offset(span.start_line);
  if (span.end_line < total) {
    return Range{start, buf.line_start_offset(span.end_line)};
  }
  if (span.start_line > 0) {
    start -= 1;
  }
  return Range{start, buf.size()};
}

}  // namespace

void execute_operator_charwise(OperatorKind op, Range range, Editor& ed,
                                char register_name) {
  if (range.start >= range.end) {
    return;
  }
  std::string text = ed.buffer().text_range(range.start, range.end - range.start);
  ed.set_register(register_name, text, false);

  if (op == OperatorKind::Yank) {
    ed.set_cursor(ed.offset_to_cursor(range.start));
    return;
  }

  ed.begin_undo_group();
  ed.erase_range(range.start, range.end - range.start);
  ed.set_cursor(ed.offset_to_cursor(range.start));
}

void execute_operator_linewise(OperatorKind op, size_t start_line, int count,
                                Editor& ed, char register_name) {
  LinewiseSpan span = linewise_span(ed.buffer(), start_line, count);
  std::string text = linewise_text(ed.buffer(), span);
  ed.set_register(register_name, text, true);

  if (op == OperatorKind::Yank) {
    ed.set_cursor(Cursor{span.start_line, 0});
    return;
  }

  bool ran_through_end = span.end_line >= ed.buffer().line_count();
  Range erase = linewise_erase_range(ed.buffer(), span);

  ed.begin_undo_group();
  ed.erase_range(erase.start, erase.end - erase.start);

  if (op == OperatorKind::Delete) {
    size_t total_after = ed.buffer().line_count();
    size_t landing_line = std::min(span.start_line, total_after - 1);
    ed.set_cursor(Cursor{landing_line, 0});
    ed.clamp_cursor_to_line();
    return;
  }

  // Change: leave one empty line at the erase point to type into.
  ed.insert_text(erase.start, "\n");
  size_t cursor_offset = ran_through_end ? ed.buffer().size() : erase.start;
  ed.set_cursor(ed.offset_to_cursor(cursor_offset));
}

void execute_delete_char(int count, Editor& ed, char register_name) {
  size_t offset = ed.cursor_offset();
  size_t line_len = ed.current_line_length();
  size_t col = ed.cursor().col;
  size_t available = (line_len > col) ? (line_len - col) : 0;
  size_t n = std::min(static_cast<size_t>(std::max(count, 1)), available);
  if (n == 0) {
    return;
  }
  std::string text = ed.buffer().text_range(offset, n);
  ed.set_register(register_name, text, false);
  ed.begin_undo_group();
  ed.erase_range(offset, n);
  ed.clamp_cursor_to_line();
}

void execute_paste(bool before, int count, Editor& ed, char register_name) {
  const RegisterContent& reg = ed.register_content(register_name);
  if (reg.text.empty()) {
    return;
  }
  int n = std::max(count, 1);
  std::string block;
  block.reserve(reg.text.size() * static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    block += reg.text;
  }

  ed.begin_undo_group();

  if (reg.linewise) {
    Cursor c = ed.cursor();
    size_t total = ed.buffer().line_count();
    size_t target_line = before ? c.line : std::min(c.line + 1, total);
    if (target_line < total) {
      ed.insert_text(ed.buffer().line_start_offset(target_line), block);
    } else {
      // Pasting past the last line of a buffer with no trailing newline:
      // that line's text runs right up to buffer.size(), so a plain insert
      // there would concatenate onto it instead of starting a new line.
      // The pasted content becomes the new last line, so it keeps the
      // file's "no trailing newline" convention rather than gaining one.
      size_t offset = ed.buffer().size();
      std::string to_insert = block;
      if (!to_insert.empty() && to_insert.back() == '\n') {
        to_insert.pop_back();
      }
      if (offset > 0) {
        to_insert = "\n" + to_insert;
      }
      ed.insert_text(offset, to_insert);
    }
    ed.set_cursor(Cursor{target_line, 0});
    return;
  }

  size_t offset = ed.cursor_offset();
  if (!before && ed.current_line_length() > 0) {
    offset += 1;
  }
  ed.insert_text(offset, block);
  ed.set_cursor(ed.offset_to_cursor(offset + block.size() - 1));
}

}  // namespace zedit::core
