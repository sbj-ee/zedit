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
using zedit::core::EditingStyle;
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

// Thin colored bar at the left edge of the gutter for each line that
// differs from the git HEAD commit -- the standard "git gutter" visual
// language (VSCode, GitLens, vim-gitgutter), deliberately a bar in the
// margin rather than a full-row tint like draw_diff_backgrounds, so it
// reads as a different, lighter-weight signal ("this line changed since
// the last commit") from the heavier ":diff two files" comparison view.
// Only Added is meaningful here -- see git_diff_status()'s own doc
// comment on why a line-based diff can't distinguish "modified" from
// "removed old + added new."
void draw_git_gutter_markers(ImDrawList* draw_list, ImVec2 origin,
                              const std::vector<VisualRow>& rows,
                              const std::vector<DiffLineStatus>& git_status, float line_height) {
  constexpr float kBarWidth = 3.0f;
  constexpr float kBarInsetX = 2.0f;
  ImU32 color = IM_COL32(90, 180, 90, 200);
  for (size_t i = 0; i < rows.size(); ++i) {
    size_t line = rows[i].buffer_line;
    if (line >= git_status.size() || git_status[line] != DiffLineStatus::Added) {
      continue;
    }
    float y = origin.y + static_cast<float>(i) * line_height;
    draw_list->AddRectFilled(ImVec2(origin.x + kBarInsetX, y),
                              ImVec2(origin.x + kBarInsetX + kBarWidth, y + line_height), color);
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

// Precise column -> x offset within a row's text, matching draw_line_text's
// own glyph-accurate advance measurement. `col * char_width` (a fixed
// width derived from a single "M" glyph) accumulates fractional
// per-glyph rounding error that grows to roughly a full character's
// worth of drift by column 10-12, even in a "monospace" font -- every
// place that positions something at a specific column (cursor, selection,
// bracket highlight, diagnostic squiggle) must agree pixel-for-pixel with
// where draw_line_text actually put that column's glyph, or they visibly
// drift apart on any line longer than about ten characters. Confirmed
// live: a block cursor at column 11 rendered a full cell to the right of
// the actual character there.
float col_offset_x(std::string_view row_text, size_t col_within_row) {
  size_t n = std::min(col_within_row, row_text.size());
  return ImGui::CalcTextSize(row_text.data(), row_text.data() + n).x;
}

// The inverse of col_offset_x: which column's glyph boundary is closest to
// a given pixel offset within a row. Mouse hit-testing needs this same
// precise per-glyph measurement, not a fixed char_width estimate (`rel_x /
// char_width`) -- that estimate drifts from the real glyph boundaries by
// roughly a full character's width by column 10-12 (see col_offset_x's own
// comment), so a drag that visually stops at some word boundary could
// silently resolve several columns further right, expanding the selection
// -- and what Ctrl-C copies -- past where the mouse actually is. Confirmed
// live: this was letting mouse-drag selections copy more text than what
// was highlighted on screen.
size_t col_for_x(std::string_view row_text, float rel_x) {
  size_t n = row_text.size();
  float prev_x = 0.0f;
  for (size_t col = 1; col <= n; ++col) {
    float x = col_offset_x(row_text, col);
    if (x >= rel_x) {
      return (rel_x - prev_x <= x - rel_x) ? col - 1 : col;
    }
    prev_x = x;
  }
  return n;
}

// The row's text this Editor/PieceTable position falls on, already offset
// to start at row.start_col (so column arithmetic elsewhere can treat
// column 0 of this string as row.start_col), or an empty string if
// `row.buffer_line` is out of range.
std::string row_text_for(const PieceTable& buf, const VisualRow& row) {
  std::string line = buf.line_text(row.buffer_line);
  if (row.start_col >= line.size()) {
    return {};
  }
  return line.substr(row.start_col, row.end_col - row.start_col);
}

void draw_diagnostics(ImDrawList* draw_list, const Editor& ed, const PieceTable& buf,
                       ImVec2 origin, const std::vector<VisualRow>& rows, float line_height) {
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
      std::string text = row_text_for(buf, row);
      float y = origin.y + static_cast<float>(i) * line_height + line_height - 3.0f;
      float x0 = origin.x + col_offset_x(text, start_col - row.start_col);
      float x1 = origin.x + col_offset_x(text, end_col - row.start_col);
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

void draw_selection(ImDrawList* draw_list, const Editor& ed, const PieceTable& buf, ImVec2 origin,
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
    std::string text = row_text_for(buf, row);
    float y = origin.y + static_cast<float>(i) * line_height;
    float x0 = origin.x + col_offset_x(text, start_col - row.start_col);
    // The selection's exclusive end can be one past the row's own text
    // (see the VisualLine/+1 cases above), for which col_offset_x's own
    // clamp-to-size falls one glyph short of the intended "through the
    // end of line" width -- pad by a fixed char_width in exactly that
    // case so a linewise/end-of-line selection still visibly reaches the
    // row's edge instead of stopping one glyph early.
    size_t text_col_end = end_col - row.start_col;
    float x1 = origin.x + (text_col_end > text.size()
                                ? col_offset_x(text, text.size()) +
                                      static_cast<float>(text_col_end - text.size()) * char_width
                                : col_offset_x(text, text_col_end));
    draw_list->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + line_height), color);
  }
}

// An outlined (not filled, to stay legible over the cursor block/selection)
// box around one character -- used for both halves of a matching bracket
// pair. A no-op if `pos` isn't on any currently-visible row.
void draw_bracket_highlight(ImDrawList* draw_list, const PieceTable& buf, ImVec2 origin,
                             const std::vector<VisualRow>& rows, float line_height, Cursor pos) {
  for (size_t i = 0; i < rows.size(); ++i) {
    const VisualRow& row = rows[i];
    if (row.buffer_line != pos.line || pos.col < row.start_col || pos.col >= row.end_col) {
      continue;
    }
    std::string text = row_text_for(buf, row);
    size_t col = pos.col - row.start_col;
    float x0 = origin.x + col_offset_x(text, col);
    float x1 = origin.x + col_offset_x(text, col + 1);
    float y = origin.y + static_cast<float>(i) * line_height;
    draw_list->AddRect(ImVec2(x0, y), ImVec2(x1, y + line_height), IM_COL32(255, 215, 0, 220),
                        0.0f, 0, 1.5f);
    return;
  }
}

bool handle_mouse_click(Editor& ed, const PieceTable& buf, ImVec2 origin,
                         const std::vector<VisualRow>& rows, float char_width, float line_height) {
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
  // True every frame the left button is held and has moved past a small
  // threshold since it went down -- distinct from left_clicked, which
  // only fires once on the down-edge. Checked every frame (not just
  // once) so the selection keeps extending as the mouse keeps moving.
  bool dragging =
      ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f);
  if (!left_clicked && !right_clicked && !dragging) {
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
  // Precise per-glyph hit-testing (col_for_x), not a fixed char_width
  // division -- must agree with where draw_selection actually renders each
  // column (col_offset_x) or a drag's selection silently extends past
  // where the mouse visually is. See col_for_x's own comment.
  size_t rel_col = col_for_x(row_text_for(buf, row), rel_x);
  rel_col = std::min(rel_col, max_col_in_row);
  Cursor target{row.buffer_line, row.start_col + rel_col};

  if (dragging) {
    bool already_selecting = (ed.mode() == Mode::Visual || ed.mode() == Mode::VisualLine);
    bool may_start_selecting;
    if (already_selecting) {
      may_start_selecting = true;
    } else if (ed.editing_style() == EditingStyle::Gedit) {
      may_start_selecting = true;  // gedit style: Insert is the only non-selecting mode
    } else {
      // Vim style only starts a drag-selection from Normal -- starting
      // mid-Insert would abandon that Insert session's dot-repeat
      // tracking (insert_session_text_, pending_change_) without
      // properly closing it the way Escape does.
      may_start_selecting = (ed.mode() == Mode::Normal);
    }
    if (!may_start_selecting) {
      return false;  // e.g. vim style mid-Insert -- ignore the drag entirely
    }
    if (!already_selecting) {
      ed.start_visual_selection();
    }
    ed.set_cursor(target);
    return true;
  }

  ed.set_cursor(target);

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

// Marks each space character in a row with a small faint dot centered in
// its cell, the same "visible whitespace" convention VS Code/Sublime use.
// Drawn as a circle rather than relying on AddText for some placeholder
// glyph -- a literal U+0020 has no ink of its own, so text rendering alone
// can never make it visible. Positions come from col_offset_x (the same
// per-glyph measurement draw_line_text and the cursor use), not
// col * char_width, so the dot lands in the actual cell even where prior
// glyphs' advances have drifted from the "M"-derived estimate.
void draw_whitespace_dots(ImDrawList* draw_list, std::string_view text, ImVec2 row_origin,
                           float line_height) {
  ImU32 color = whitespace_dot_color();
  float radius = std::clamp(line_height * 0.07f, 1.0f, 2.5f);
  for (size_t col = 0; col < text.size(); ++col) {
    if (text[col] != ' ') {
      continue;
    }
    float cell_start = col_offset_x(text, col);
    float cell_end = col_offset_x(text, col + 1);
    ImVec2 center(row_origin.x + (cell_start + cell_end) * 0.5f,
                  row_origin.y + line_height * 0.5f);
    draw_list->AddCircleFilled(center, radius, color);
  }
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

bool TextView::render(Editor& ed, ImFont* font, float height, float width, bool word_wrap,
                       bool show_whitespace) {
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

  bool clicked = handle_mouse_click(ed, buf, text_origin, rows, char_width, line_height);

  draw_diff_backgrounds(draw_list, ed, text_origin, rows, pane_width, line_height);
  draw_selection(draw_list, ed, buf, text_origin, rows, char_width, line_height);

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
    if (show_whitespace) {
      draw_whitespace_dots(draw_list, row_text, ImVec2(text_origin.x, y), line_height);
    }
  }

  draw_diagnostics(draw_list, ed, buf, text_origin, rows, line_height);
  draw_line_numbers(draw_list, origin, rows, gutter_width, line_height, ed.cursor().line);
  draw_git_gutter_markers(draw_list, origin, rows, ed.git_diff_status(), line_height);

  Cursor cursor = ed.cursor();

  std::string cursor_line_text = buf.line_text(cursor.line);
  if (cursor.col < cursor_line_text.size() && is_bracket_char(cursor_line_text[cursor.col])) {
    std::optional<size_t> match = motion_matching_bracket(buf, ed.cursor_offset());
    if (match) {
      Cursor match_pos = ed.offset_to_cursor(*match);
      draw_bracket_highlight(draw_list, buf, text_origin, rows, line_height, cursor);
      draw_bracket_highlight(draw_list, buf, text_origin, rows, line_height, match_pos);
    }
  }

  float cursor_x = text_origin.x;
  float cursor_y = text_origin.y;
  // The actual glyph width at the cursor's column, not a fixed
  // char_width -- so the block cursor's right edge lands exactly where
  // the *next* column's glyph starts, matching col_offset_x's precision.
  float cursor_glyph_width = char_width;
  for (size_t i = 0; i < rows.size(); ++i) {
    const VisualRow& row = rows[i];
    if (row.buffer_line == cursor.line && cursor.col >= row.start_col &&
        cursor.col <= row.end_col) {
      std::string text = row_text_for(buf, row);
      size_t col = cursor.col - row.start_col;
      cursor_x = text_origin.x + col_offset_x(text, col);
      cursor_y = text_origin.y + static_cast<float>(i) * line_height;
      if (col < text.size()) {
        cursor_glyph_width = col_offset_x(text, col + 1) - col_offset_x(text, col);
      }
      break;
    }
  }
  cursor_screen_pos_ = ImVec2(cursor_x, cursor_y);
  bool block_cursor = (ed.mode() != Mode::Insert);
  float cursor_width = block_cursor ? cursor_glyph_width : std::max(2.0f, char_width * 0.15f);
  draw_list->AddRectFilled(
      ImVec2(cursor_x, cursor_y), ImVec2(cursor_x + cursor_width, cursor_y + line_height),
      IM_COL32(210, 200, 80, block_cursor ? 130 : 220));

  ImGui::EndChild();
  ImGui::PopFont();
  return clicked;
}

}  // namespace zedit::frontend
