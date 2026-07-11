#include "toolbar.hpp"

#include <imgui.h>

#include "zedit/core/file_io.hpp"

namespace zedit::frontend {

using zedit::core::Editor;
using zedit::core::FileIoError;
using zedit::core::Key;
using zedit::core::KeyEvent;

void render_toolbar(Editor& ed) {
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
  ImGui::SameLine();
  ImGui::TextUnformatted("|");
  ImGui::SameLine();
  if (ImGui::Button("Undo")) {
    ed.undo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Redo")) {
    ed.redo();
  }
  ImGui::SameLine();
  ImGui::TextUnformatted("|");
  ImGui::SameLine();
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
  ImGui::SameLine();
  ImGui::TextUnformatted("|");
  ImGui::SameLine();
  if (ImGui::Button("Split H")) {
    ed.split_horizontal();
  }
  ImGui::SameLine();
  if (ImGui::Button("Split V")) {
    ed.split_vertical();
  }
}

}  // namespace zedit::frontend
