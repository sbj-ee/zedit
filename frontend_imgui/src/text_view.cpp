#include "text_view.hpp"

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "theme.hpp"
#include "zedit/core/diff.hpp"
#include "zedit/core/highlight.hpp"
#include "zedit/core/motion.hpp"
#include "zedit/core/word_wrap.hpp"

namespace zedit::frontend {

using zedit::core::Cursor;
using zedit::core::DiffLineStatus;
using zedit::core::Editor;
using zedit::core::HighlightSpan;
using zedit::core::is_bracket_char;
using zedit::core::Key;
using zedit::core::KeyEvent;
using zedit::core::LspDiagnostic;
using zedit::core::Mode;
using zedit::core::motion_matching_bracket;
using zedit::core::PieceTable;
using zedit::core::wrap_line;
using zedit::core::WrapSegment;

namespace {

constexpr float kGutterRightMargin = 8.0f;
constexpr float kGutterLeftMargin = 8.0f;

float compute_gutter_width(size_t total_lines, float char_width) {
  int digits = 1;
  size_t n = total_lines;
  while (n >= 10) {
    n /= 10;
    ++digits;
  }
  return kGutterLeftMargin + static_cast<float>(digits) * char_width + kGutterRightMargin;
}

// One row of the visible screen -- without word wrap this is always
// exactly one whole buffer line ([0, line.size())), same as before
// wrapping existed. With it, a long line can span several consecutive
// rows, each covering [start_col, end_col) of that *same* buffer_line.
struct VisualRow {
  size_t buffer_line;
  size_t start_col;
  size_t end_col;
};

// Builds up to `max_rows` starting at `first_line` -- bounded rather than
// covering the whole rest of the document, so cost stays O(visible
// rows), not O(file size), matching this widget's other per-frame work.
std::vector<VisualRow> build_visual_rows(const PieceTable& buf, size_t first_line, size_t max_rows,
                                          size_t max_chars, bool word_wrap) {
  std::vector<VisualRow> rows;
  size_t total_lines = buf.line_count();
  for (size_t line = first_line; line < total_lines; ++line) {
    std::string text = buf.line_text(line);
    if (!word_wrap) {
      rows.push_back(VisualRow{line, 0, text.size()});
    } else {
      for (const WrapSegment& seg : wrap_line(text, max_chars)) {
        rows.push_back(VisualRow{line, seg.start, seg.end});
        if (rows.size() >= max_rows) {
          return rows;
        }
      }
    }
    if (rows.size() >= max_rows) {
      break;
    }
  }
  return rows;
}

// Right-aligned line numbers, dimmed except for the cursor's own line.
// Only drawn on a buffer line's *first* visual row, so a wrapped line's
// continuation rows don't repeat the same number.
void draw_line_numbers(ImDrawList* draw_list, ImVec2 origin, const std::vector<VisualRow>& rows,
                        float gutter_width, float line_height, size_t cursor_line) {
  ImU32 dim_color = IM_COL32(120, 120, 120, 200);
  for (size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].start_col != 0) {
      continue;
    }
    std::string label = std::to_string(rows[i].buffer_line + 1);
    float text_width = ImGui::CalcTextSize(label.c_str()).x;
    float x = origin.x + gutter_width - kGutterRightMargin - text_width;
    float y = origin.y + static_cast<float>(i) * line_height;
    draw_list->AddText(ImVec2(x, y),
                        rows[i].buffer_line == cursor_line ? default_text_color() : dim_color,
                        label.c_str());
  }
}

// Squiggly underline beneath each diagnostic's range (clipped to this
// row), colored by severity. Drawn as a small zigzag rather than a
// straight AddLine so it reads as "diagnostic" at a glance, distinct from
// the selection/diff backgrounds.
void draw_squiggle(ImDrawList* draw_list, float x0, float x1, float y, ImU32 color) {
  constexpr float kAmplitude = 2.0f;
  constexpr float kPeriod = 4.0f;
  float x = x0;
  bool up = true;
  while (x < x1) {
    float next_x = std::min(x + kPeriod, x1);
    draw_list->AddLine(ImVec2(x, y + (up ? 0.0f : kAmplitude)),
                        ImVec2(next_x, y + (up ? kAmplitude : 0.0f)), color, 1.5f);
    x = next_x;
    up = !up;
  }
}

void draw_diagnostics(ImDrawList* draw_list, const Editor& ed, ImVec2 origin,
                       const std::vector<VisualRow>& rows, float char_width, float line_height) {
  std::vector<LspDiagnostic> diags = ed.diagnostics();
  if (diags.empty()) {
    return;
  }
  for (const LspDiagnostic& diag : diags) {
    ImU32 color = color_for_severity(diag.severity);
    for (size_t i = 0; i < rows.size(); ++i) {
      const VisualRow& row = rows[i];
      if (row.buffer_line < diag.start_line || row.buffer_line > diag.end_line) {
        continue;
      }
      size_t diag_start = (row.buffer_line == diag.start_line) ? diag.start_col : 0;
      size_t diag_end = (row.buffer_line == diag.end_line) ? diag.end_col : row.end_col;
      diag_end = std::max(diag_end, diag_start + 1);  // zero-width diagnostics get 1 char
      size_t start_col = std::max(diag_start, row.start_col);
      size_t end_col = std::min(diag_end, row.end_col);
      if (start_col >= end_col) {
        continue;
      }
      float y = origin.y + static_cast<float>(i) * line_height + line_height - 3.0f;
      float x0 = origin.x + static_cast<float>(start_col - row.start_col) * char_width;
      float x1 = origin.x + static_cast<float>(end_col - row.start_col) * char_width;
      draw_squiggle(draw_list, x0, x1, y, color);
    }
  }
}

// Full-width per-row background for :diff mode -- added lines get a
// green tint, removed (relative to the other side) a red one. Drawn
// before the text so the colored text still reads clearly on top. Diff
// status is per buffer line, so every visual row of a wrapped line gets
// the same tint.
void draw_diff_backgrounds(ImDrawList* draw_list, const Editor& ed, ImVec2 origin,
                            const std::vector<VisualRow>& rows, float pane_width,
                            float line_height) {
  if (!ed.is_diffing()) {
    return;
  }
  std::vector<DiffLineStatus> statuses = ed.diff_status_for_window(ed.current_window_index());
  for (size_t i = 0; i < rows.size(); ++i) {
    size_t line = rows[i].buffer_line;
    if (line >= statuses.size()) {
      continue;
    }
    DiffLineStatus status = statuses[line];
    if (status == DiffLineStatus::Unchanged) {
      continue;
    }
    ImU32 color = (status == DiffLineStatus::Added) ? IM_COL32(60, 130, 70, 90)
                                                      : IM_COL32(150, 60, 60, 90);
    float y = origin.y + static_cast<float>(i) * line_height;
    draw_list->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + pane_width, y + line_height),
                              color);
  }
}

void draw_selection(ImDrawList* draw_list, const Editor& ed, ImVec2 origin,
                     const std::vector<VisualRow>& rows, float char_width, float line_height) {
  Mode mode = ed.mode();
  if (mode != Mode::Visual && mode != Mode::VisualLine) {
    return;
  }

  Cursor a = ed.visual_anchor();
  Cursor b = ed.cursor();
  Cursor lo = (a.line < b.line || (a.line == b.line && a.col <= b.col)) ? a : b;
  Cursor hi = (a.line < b.line || (a.line == b.line && a.col <= b.col)) ? b : a;

  ImU32 color = IM_COL32(90, 110, 160, 110);

  for (size_t i = 0; i < rows.size(); ++i) {
    const VisualRow& row = rows[i];
    if (row.buffer_line < lo.line || row.buffer_line > hi.line) {
      continue;
    }
    size_t sel_start = (mode == Mode::VisualLine) ? 0 : (row.buffer_line == lo.line) ? lo.col : 0;
    size_t sel_end = (mode == Mode::VisualLine)        ? row.end_col + 1
                      : (row.buffer_line == hi.line) ? hi.col + 1
                                                      : row.end_col + 1;
    size_t start_col = std::max(sel_start, row.start_col);
    size_t end_col = std::min(sel_end, row.end_col + 1);
    if (start_col >= end_col) {
      continue;
    }
    float y = origin.y + static_cast<float>(i) * line_height;
    float x0 = origin.x + static_cast<float>(start_col - row.start_col) * char_width;
    float x1 = origin.x + static_cast<float>(end_col - row.start_col) * char_width;
    draw_list->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + line_height), color);
  }
}

// An outlined (not filled, to stay legible over the cursor block/selection)
// box around one character -- used for both halves of a matching bracket
// pair. A no-op if `pos` isn't on any currently-visible row.
void draw_bracket_highlight(ImDrawList* draw_list, ImVec2 origin, const std::vector<VisualRow>& rows,
                             float char_width, float line_height, Cursor pos) {
  for (size_t i = 0; i < rows.size(); ++i) {
    const VisualRow& row = rows[i];
    if (row.buffer_line != pos.line || pos.col < row.start_col || pos.col >= row.end_col) {
      continue;
    }
    float x = origin.x + static_cast<float>(pos.col - row.start_col) * char_width;
    float y = origin.y + static_cast<float>(i) * line_height;
    draw_list->AddRect(ImVec2(x, y), ImVec2(x + char_width, y + line_height),
                        IM_COL32(255, 215, 0, 220), 0.0f, 0, 1.5f);
    return;
  }
}

bool handle_mouse_click(Editor& ed, ImVec2 origin, const std::vector<VisualRow>& rows,
                         float char_width, float line_height) {
  if (!ImGui::IsWindowHovered()) {
    return false;
  }
  bool left_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  bool right_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
  // The second click of a double-click also fires IsMouseClicked (left)
  // for that same click, so double_clicked implies left_clicked -- no
  // conflict between positioning the cursor and then also selecting the
  // word there.
  bool double_clicked = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
  if (!left_clicked && !right_clicked) {
    return false;
  }
  if (rows.empty() || char_width <= 0.0f || line_height <= 0.0f) {
    return false;
  }
  ImVec2 mouse = ImGui::GetMousePos();
  float rel_x = mouse.x - origin.x;
  float rel_y = mouse.y - origin.y;
  if (rel_x < 0.0f || rel_y < 0.0f) {
    return false;
  }

  size_t row_index = std::min(static_cast<size_t>(rel_y / line_height), rows.size() - 1);
  const VisualRow& row = rows[row_index];

  size_t row_len = row.end_col - row.start_col;
  size_t max_col_in_row = (ed.mode() == Mode::Insert) ? row_len : (row_len > 0 ? row_len - 1 : 0);
  size_t rel_col = static_cast<size_t>(std::max(rel_x / char_width + 0.5f, 0.0f));
  rel_col = std::min(rel_col, max_col_in_row);

  ed.set_cursor(Cursor{row.buffer_line, row.start_col + rel_col});

  if (right_clicked) {
    // Paste at the click position -- reuses whatever Ctrl-P already does
    // in the current mode (Normal: vim's linewise 'p'; Insert: splice
    // the register's raw text at the cursor), rather than a new "paste
    // here" code path, matching a GUI editor's own right-click-paste.
    ed.handle_key(KeyEvent{Key::CtrlP, 0});
  } else if (double_clicked) {
    ed.select_word_at_cursor();
  }
  return true;
}

// Draws one row's text as a sequence of colored segments, splitting at
// each highlight span's boundaries (clipped to this row) and using the
// default color for whatever falls between spans. Advances x by each run's
// actual measured width (via CalcTextSize) rather than assuming
// char_width * byte_count -- with a real font, per-glyph advances can
// differ from the "M"-derived char_width by fractions of a pixel, and that
// error accumulates into visible gaps after several segments on one row.
void draw_line_text(ImDrawList* draw_list, std::string_view text, size_t row_start_byte,
                     const std::vector<HighlightSpan>& visible_spans, ImVec2 row_origin) {
  size_t row_end_byte = row_start_byte + text.size();

  std::vector<HighlightSpan> segs;
  for (const HighlightSpan& s : visible_spans) {
    if (s.end_byte <= row_start_byte || s.start_byte >= row_end_byte) {
      continue;
    }
    segs.push_back(HighlightSpan{std::max(s.start_byte, row_start_byte),
                                  std::min(s.end_byte, row_end_byte), s.kind});
  }
  std::sort(segs.begin(), segs.end(),
            [](const HighlightSpan& a, const HighlightSpan& b) { return a.start_byte < b.start_byte; });

  size_t pos = row_start_byte;
  float x = row_origin.x;
  auto draw_run = [&](size_t start, size_t end, ImU32 color) {
    if (end <= start) return;
    const char* begin = text.data() + (start - row_start_byte);
    const char* stop = text.data() + (end - row_start_byte);
    draw_list->AddText(ImVec2(x, row_origin.y), color, begin, stop);
    x += ImGui::CalcTextSize(begin, stop).x;
  };

  for (const HighlightSpan& seg : segs) {
    if (seg.start_byte < pos) {
      continue;  // overlapping capture on already-drawn ground; skip
    }
    draw_run(pos, seg.start_byte, default_text_color());
    draw_run(seg.start_byte, seg.end_byte, color_for_token(seg.kind));
    pos = seg.end_byte;
  }
  draw_run(pos, row_end_byte, default_text_color());
}

}  // namespace

void TextView::scroll_to_keep_cursor_visible(const Editor& ed, size_t visible_rows) {
  // Buffer-line-based, not visual-row-based: with word wrap on, this
  // guarantees the cursor's *line* starts within view, but if enough
  // wrapped rows precede it, its actual wrapped row could still land just
  // past the bottom. Accepted imprecision -- exact wrapped-row-aware
  // scrolling would need to walk wrap_line() over every intervening line
  // on every render, and this editor's target files don't have lines
  // long/frequent enough for the gap to matter in practice.
  size_t cursor_line = ed.cursor().line;
  if (cursor_line < first_visible_line_) {
    first_visible_line_ = cursor_line;
  } else if (visible_rows > 0 && cursor_line >= first_visible_line_ + visible_rows) {
    first_visible_line_ = cursor_line - visible_rows + 1;
  }
}

bool TextView::render(Editor& ed, ImFont* font, float height, float width, bool word_wrap) {
  ImGui::PushFont(font);

  ImVec2 char_size = ImGui::CalcTextSize("M");
  float char_width = char_size.x;
  float line_height = ImGui::GetTextLineHeight();
  size_t visible_rows = line_height > 0.0f ? static_cast<size_t>(height / line_height) : 1;
  visible_rows = std::max<size_t>(visible_rows, 1);

  size_t cursor_line_now = ed.cursor().line;
  if (cursor_line_now != last_cursor_line_) {
    scroll_to_keep_cursor_visible(ed, visible_rows);
    last_cursor_line_ = cursor_line_now;
  }

  ImGui::BeginChild("zedit_text_view", ImVec2(width, height), true,
                     ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoScrollWithMouse);

  const PieceTable& buf = ed.buffer();
  size_t total_lines = buf.line_count();

  if (ImGui::IsWindowHovered()) {
    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) {
      constexpr size_t kLinesPerScrollTick = 3;
      size_t max_first = total_lines > 0 ? total_lines - 1 : 0;
      if (wheel < 0.0f) {
        size_t down = static_cast<size_t>(-wheel * static_cast<float>(kLinesPerScrollTick) + 0.5f);
        first_visible_line_ = std::min(first_visible_line_ + down, max_first);
      } else {
        size_t up = static_cast<size_t>(wheel * static_cast<float>(kLinesPerScrollTick) + 0.5f);
        first_visible_line_ = (up > first_visible_line_) ? 0 : first_visible_line_ - up;
      }
    }
  }

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImVec2 origin = ImGui::GetCursorScreenPos();

  float gutter_width = compute_gutter_width(total_lines, char_width);
  ImVec2 text_origin(origin.x + gutter_width, origin.y);
  float pane_width = ImGui::GetWindowSize().x - gutter_width;
  size_t max_chars =
      char_width > 0.0f ? static_cast<size_t>(pane_width / char_width) : 0;
  max_chars = std::max<size_t>(max_chars, 1);

  std::vector<VisualRow> rows =
      build_visual_rows(buf, first_visible_line_, visible_rows, max_chars, word_wrap);

  bool clicked = handle_mouse_click(ed, text_origin, rows, char_width, line_height);

  draw_diff_backgrounds(draw_list, ed, text_origin, rows, pane_width, line_height);
  draw_selection(draw_list, ed, text_origin, rows, char_width, line_height);

  size_t viewport_start_byte = buf.line_start_offset(first_visible_line_);
  size_t viewport_end_line = rows.empty() ? first_visible_line_ : rows.back().buffer_line + 1;
  size_t viewport_end_byte =
      (viewport_end_line < total_lines) ? buf.line_start_offset(viewport_end_line) : buf.size();
  std::vector<HighlightSpan> visible_spans =
      ed.highlighter().highlight(viewport_start_byte, viewport_end_byte);

  for (size_t i = 0; i < rows.size(); ++i) {
    const VisualRow& row = rows[i];
    float y = text_origin.y + static_cast<float>(i) * line_height;
    std::string line_text = buf.line_text(row.buffer_line);
    std::string_view row_text(line_text.data() + row.start_col, row.end_col - row.start_col);
    size_t line_start_byte = buf.line_start_offset(row.buffer_line);
    draw_line_text(draw_list, row_text, line_start_byte + row.start_col, visible_spans,
                    ImVec2(text_origin.x, y));
  }

  draw_diagnostics(draw_list, ed, text_origin, rows, char_width, line_height);
  draw_line_numbers(draw_list, origin, rows, gutter_width, line_height, ed.cursor().line);

  Cursor cursor = ed.cursor();

  std::string cursor_line_text = buf.line_text(cursor.line);
  if (cursor.col < cursor_line_text.size() && is_bracket_char(cursor_line_text[cursor.col])) {
    std::optional<size_t> match = motion_matching_bracket(buf, ed.cursor_offset());
    if (match) {
      Cursor match_pos = ed.offset_to_cursor(*match);
      draw_bracket_highlight(draw_list, text_origin, rows, char_width, line_height, cursor);
      draw_bracket_highlight(draw_list, text_origin, rows, char_width, line_height, match_pos);
    }
  }

  float cursor_x = text_origin.x + static_cast<float>(cursor.col) * char_width;
  float cursor_y = text_origin.y;
  for (size_t i = 0; i < rows.size(); ++i) {
    const VisualRow& row = rows[i];
    if (row.buffer_line == cursor.line && cursor.col >= row.start_col &&
        cursor.col <= row.end_col) {
      cursor_x = text_origin.x + static_cast<float>(cursor.col - row.start_col) * char_width;
      cursor_y = text_origin.y + static_cast<float>(i) * line_height;
      break;
    }
  }
  cursor_screen_pos_ = ImVec2(cursor_x, cursor_y);
  bool block_cursor = (ed.mode() != Mode::Insert);
  float cursor_width = block_cursor ? char_width : std::max(2.0f, char_width * 0.15f);
  draw_list->AddRectFilled(
      ImVec2(cursor_x, cursor_y), ImVec2(cursor_x + cursor_width, cursor_y + line_height),
      IM_COL32(210, 200, 80, block_cursor ? 130 : 220));

  ImGui::EndChild();
  ImGui::PopFont();
  return clicked;
}

}  // namespace zedit::frontend
