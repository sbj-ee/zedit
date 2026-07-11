#include "tab_bar.hpp"

#include <imgui.h>

#include <string>

namespace zedit::frontend {

namespace {

std::string basename_of(const std::string& path) {
  if (path.empty()) {
    return "[No Name]";
  }
  size_t slash = path.find_last_of('/');
  return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

}  // namespace

void render_tab_bar(zedit::core::Editor& ed) {
  size_t count = ed.buffer_count();
  size_t active_index = ed.current_buffer_index();

  for (size_t i = 0; i < count; ++i) {
    ImGui::PushID(static_cast<int>(i));

    bool active = (i == active_index);
    std::string label = basename_of(ed.buffer_filename(i));
    if (ed.buffer_dirty(i)) {
      label += " *";
    }

    if (active) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.36f, 0.58f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.42f, 0.66f, 1.0f));
    }
    if (ImGui::Button(label.c_str())) {
      ed.switch_to_buffer(i);
    }
    if (active) {
      ImGui::PopStyleColor(2);
    }

    ImGui::PopID();
    if (i + 1 < count) {
      ImGui::SameLine();
    }
  }
}

}  // namespace zedit::frontend
