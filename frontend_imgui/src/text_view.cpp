#include "text_view.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace zedit::frontend {

using zedit::core::Cursor;
using zedit::core::Editor;
using zedit::core::Mode;
using zedit::core::PieceTable;

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
