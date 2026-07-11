#include "toolbar.hpp"

#include <imgui.h>

#include "zedit/core/file_io.hpp"

namespace zedit::frontend {

using zedit::core::Editor;
using zedit::core::FileIoError;
using zedit::core::Key;
using zedit::core::KeyEvent;

void draw_vertical_separator() {
  ImGui::SameLine();
  ImGui::Dummy(ImVec2(1.0f, ImGui::GetFrameHeight()));
  ImVec2 p0 = ImGui::GetItemRectMin();
  ImVec2 p1 = ImGui::GetItemRectMax();
  float x = (p0.x + p1.x) * 0.5f;
  ImGui::GetWindowDrawList()->AddLine(ImVec2(x, p0.y + 2.0f), ImVec2(x, p1.y - 2.0f),
                                       ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
  ImGui::SameLine();
}

void render_toolbar(Editor& ed) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));

  if (ImGui::Button("New")) {
    ed.open_buffer("");
  }
  ImGui::SameLine();
  if (ImGui::Button("Save")) {
    try {
      ed.save();
    } catch (const FileIoError&) {
    }
  }
  draw_vertical_separator();
  if (ImGui::Button("Undo")) {
    ed.undo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Redo")) {
    ed.redo();
  }
  draw_vertical_separator();
  if (ImGui::Button("Copy")) {
    ed.handle_key(KeyEvent{Key::CtrlC, 0});
  }
  ImGui::SameLine();
  if (ImGui::Button("Paste")) {
    ed.handle_key(KeyEvent{Key::CtrlP, 0});
  }
  ImGui::SameLine();
  if (ImGui::Button("Select All")) {
    ed.handle_key(KeyEvent{Key::CtrlA, 0});
  }
  draw_vertical_separator();
  if (ImGui::Button("Split H")) {
    ed.split_horizontal();
  }
  ImGui::SameLine();
  if (ImGui::Button("Split V")) {
    ed.split_vertical();
  }

  ImGui::PopStyleVar(3);
}

}  // namespace zedit::frontend
