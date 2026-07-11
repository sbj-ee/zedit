#include "text_view.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace zedit::frontend {

using zedit::core::Cursor;
using zedit::core::Editor;
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

  for (size_t line = first_visible_line_; line < last_line; ++line) {
    float y = origin.y +
               static_cast<float>(line - first_visible_line_) * line_height;
    std::string text = buf.line_text(line);
    draw_list->AddText(ImVec2(origin.x, y), IM_COL32(220, 220, 220, 255),
                        text.c_str(), text.c_str() + text.size());
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
