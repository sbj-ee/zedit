#include "text_view.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

#include "theme.hpp"
#include "zedit/core/highlight.hpp"

namespace zedit::frontend {

using zedit::core::Cursor;
using zedit::core::Editor;
using zedit::core::HighlightSpan;
using zedit::core::Mode;
using zedit::core::PieceTable;

namespace {

void draw_selection(ImDrawList* draw_list, const Editor& ed, ImVec2 origin,
                     size_t first_visible_line, size_t last_visible_line,
                     float char_width, float line_height) {
  Mode mode = ed.mode();
  if (mode != Mode::Visual && mode != Mode::VisualLine) {
    return;
  }

  Cursor a = ed.visual_anchor();
  Cursor b = ed.cursor();
  Cursor lo = (a.line < b.line || (a.line == b.line && a.col <= b.col)) ? a : b;
  Cursor hi = (a.line < b.line || (a.line == b.line && a.col <= b.col)) ? b : a;

  const PieceTable& buf = ed.buffer();
  ImU32 color = IM_COL32(90, 110, 160, 110);

  for (size_t line = std::max(first_visible_line, lo.line);
       line <= hi.line && line < last_visible_line; ++line) {
    size_t line_len = buf.line_text(line).size();
    size_t start_col = (mode == Mode::VisualLine) ? 0
                        : (line == lo.line)        ? lo.col
                                                    : 0;
    size_t end_col = (mode == Mode::VisualLine) ? line_len + 1
                      : (line == hi.line)        ? hi.col + 1
                                                  : line_len + 1;
    float y = origin.y + static_cast<float>(line - first_visible_line) * line_height;
    float x0 = origin.x + static_cast<float>(start_col) * char_width;
    float x1 = origin.x + static_cast<float>(end_col) * char_width;
    draw_list->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + line_height), color);
  }
}

void handle_mouse_click(Editor& ed, ImVec2 origin, size_t first_visible_line,
                         size_t total_lines, float char_width, float line_height) {
  if (!ImGui::IsWindowHovered() || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    return;
  }
  if (total_lines == 0 || char_width <= 0.0f || line_height <= 0.0f) {
    return;
  }
  ImVec2 mouse = ImGui::GetMousePos();
  float rel_x = mouse.x - origin.x;
  float rel_y = mouse.y - origin.y;
  if (rel_x < 0.0f || rel_y < 0.0f) {
    return;
  }

  size_t clicked_line = first_visible_line + static_cast<size_t>(rel_y / line_height);
  clicked_line = std::min(clicked_line, total_lines - 1);

  size_t line_len = ed.buffer().line_text(clicked_line).size();
  size_t max_col = (ed.mode() == Mode::Insert) ? line_len : (line_len > 0 ? line_len - 1 : 0);
  size_t col = static_cast<size_t>(std::max(rel_x / char_width + 0.5f, 0.0f));
  col = std::min(col, max_col);

  ed.set_cursor(Cursor{clicked_line, col});
}

// Draws one line's text as a sequence of colored segments, splitting at
// each highlight span's boundaries (clipped to this line) and using the
// default color for whatever falls between spans. Advances x by each run's
// actual measured width (via CalcTextSize) rather than assuming
// char_width * byte_count -- with a real font, per-glyph advances can
// differ from the "M"-derived char_width by fractions of a pixel, and that
// error accumulates into visible gaps after several segments on one line.
void draw_line_text(ImDrawList* draw_list, std::string_view text, size_t line_start_byte,
                     const std::vector<HighlightSpan>& visible_spans, ImVec2 line_origin) {
  size_t line_end_byte = line_start_byte + text.size();

  std::vector<HighlightSpan> segs;
  for (const HighlightSpan& s : visible_spans) {
    if (s.end_byte <= line_start_byte || s.start_byte >= line_end_byte) {
      continue;
    }
    segs.push_back(HighlightSpan{std::max(s.start_byte, line_start_byte),
                                  std::min(s.end_byte, line_end_byte), s.kind});
  }
  std::sort(segs.begin(), segs.end(),
            [](const HighlightSpan& a, const HighlightSpan& b) { return a.start_byte < b.start_byte; });

  size_t pos = line_start_byte;
  float x = line_origin.x;
  auto draw_run = [&](size_t start, size_t end, ImU32 color) {
    if (end <= start) return;
    const char* begin = text.data() + (start - line_start_byte);
    const char* stop = text.data() + (end - line_start_byte);
    draw_list->AddText(ImVec2(x, line_origin.y), color, begin, stop);
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
  draw_run(pos, line_end_byte, default_text_color());
}

}  // namespace

void TextView::scroll_to_keep_cursor_visible(const Editor& ed,
                                              size_t visible_lines) {
  size_t cursor_line = ed.cursor().line;
  if (cursor_line < first_visible_line_) {
    first_visible_line_ = cursor_line;
  } else if (visible_lines > 0 &&
             cursor_line >= first_visible_line_ + visible_lines) {
    first_visible_line_ = cursor_line - visible_lines + 1;
  }
}

void TextView::render(Editor& ed, ImFont* font, float height) {
  ImGui::PushFont(font);

  ImVec2 char_size = ImGui::CalcTextSize("M");
  float char_width = char_size.x;
  float line_height = ImGui::GetTextLineHeight();
  size_t visible_lines =
      line_height > 0.0f
          ? static_cast<size_t>(height / line_height)
          : 1;
  visible_lines = std::max<size_t>(visible_lines, 1);

  scroll_to_keep_cursor_visible(ed, visible_lines);

  ImGui::BeginChild("zedit_text_view", ImVec2(0, height), true,
                     ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoScrollWithMouse);

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImVec2 origin = ImGui::GetCursorScreenPos();

  const PieceTable& buf = ed.buffer();
  size_t total_lines = buf.line_count();
  size_t last_line = std::min(first_visible_line_ + visible_lines, total_lines);

  handle_mouse_click(ed, origin, first_visible_line_, total_lines, char_width, line_height);

  draw_selection(draw_list, ed, origin, first_visible_line_, last_line, char_width, line_height);

  size_t viewport_start_byte = buf.line_start_offset(first_visible_line_);
  size_t viewport_end_byte = (last_line < total_lines) ? buf.line_start_offset(last_line) : buf.size();
  std::vector<HighlightSpan> visible_spans =
      ed.highlighter().highlight(viewport_start_byte, viewport_end_byte);

  for (size_t line = first_visible_line_; line < last_line; ++line) {
    float y = origin.y +
               static_cast<float>(line - first_visible_line_) * line_height;
    std::string text = buf.line_text(line);
    size_t line_start_byte = buf.line_start_offset(line);
    draw_line_text(draw_list, text, line_start_byte, visible_spans, ImVec2(origin.x, y));
  }

  Cursor cursor = ed.cursor();
  float cursor_x = origin.x + static_cast<float>(cursor.col) * char_width;
  float cursor_y =
      origin.y +
      static_cast<float>(cursor.line - first_visible_line_) * line_height;
  bool block_cursor = (ed.mode() != Mode::Insert);
  float cursor_width = block_cursor ? char_width : std::max(2.0f, char_width * 0.15f);
  draw_list->AddRectFilled(
      ImVec2(cursor_x, cursor_y), ImVec2(cursor_x + cursor_width, cursor_y + line_height),
      IM_COL32(210, 200, 80, block_cursor ? 130 : 220));

  ImGui::EndChild();
  ImGui::PopFont();
}

}  // namespace zedit::frontend
