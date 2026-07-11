#include "status_line.hpp"

#include <imgui.h>

namespace zedit::frontend {

using zedit::core::Cursor;
using zedit::core::Editor;
using zedit::core::Mode;

void render_status_line(const Editor& ed) {
  if (ed.mode() == Mode::CommandLine) {
    ImGui::Text(":%s", ed.command_line_buffer().c_str());
    return;
  }

  const char* mode_name = "NORMAL";
  switch (ed.mode()) {
    case Mode::Insert:
      mode_name = "INSERT";
      break;
    case Mode::Visual:
      mode_name = "VISUAL";
      break;
    case Mode::VisualLine:
      mode_name = "VISUAL LINE";
      break;
    default:
      break;
  }
  Cursor c = ed.cursor();
  ImGui::Text("-- %s --  %s%s  %zu:%zu", mode_name,
              ed.filename().empty() ? "[No Name]" : ed.filename().c_str(),
              ed.dirty() ? " [+]" : "", c.line + 1, c.col + 1);

  if (!ed.last_error().empty()) {
    ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "%s",
                        ed.last_error().c_str());
  }
}

}  // namespace zedit::frontend
