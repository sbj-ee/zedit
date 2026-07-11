#include "find_replace_dialog.hpp"

#include <imgui.h>

#include <array>
#include <string>

namespace zedit::frontend {

using zedit::core::Editor;

void render_find_replace_popup(Editor& ed) {
  static std::array<char, 512> find_buf{};
  static std::array<char, 512> replace_buf{};
  static std::string status;
  static bool initialized = false;

  if (!ImGui::BeginPopupModal("Find and Replace", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    // Not open (or just closed) this frame -- reset so the next open
    // re-focuses the Find field instead of leaving it stale.
    initialized = false;
    return;
  }

  if (!initialized) {
    // Without this, no widget has keyboard focus the first frame the
    // popup is open, so io.WantTextInput stays false and typing right
    // away falls through to the editor underneath instead of this
    // field -- confirmed live: typed text landed in the buffer as vim
    // keystrokes (e.g. "banana" as "b" motion, "a" enter Insert, then
    // "nana" inserted literally) instead of in the Find box.
    ImGui::SetKeyboardFocusHere();
    initialized = true;
  }
  ImGui::InputText("Find", find_buf.data(), find_buf.size());
  ImGui::InputText("Replace with", replace_buf.data(), replace_buf.size());

  bool has_pattern = find_buf[0] != '\0';

  if (ImGui::Button("Find Next") && has_pattern) {
    ed.set_last_search(find_buf.data(), true);
    status = ed.jump_to_search(true) ? "" : "Not found";
  }
  ImGui::SameLine();
  if (ImGui::Button("Replace") && has_pattern) {
    // Replaces the match the cursor is already sitting on (a no-op if
    // it isn't), then always advances to the next occurrence -- so a
    // first click just seeks the first match, and each click after
    // that replaces-then-seeks, matching gedit's own "Replace" button.
    ed.replace_current_match(find_buf.data(), replace_buf.data());
    ed.set_last_search(find_buf.data(), true);
    status = ed.jump_to_search(true) ? "" : "Not found";
  }
  ImGui::SameLine();
  if (ImGui::Button("Replace All") && has_pattern) {
    size_t count = ed.replace_all(find_buf.data(), replace_buf.data());
    status = "Replaced " + std::to_string(count) + (count == 1 ? " occurrence" : " occurrences");
  }
  ImGui::SameLine();
  if (ImGui::Button("Close")) {
    find_buf[0] = '\0';
    replace_buf[0] = '\0';
    status.clear();
    ImGui::CloseCurrentPopup();
  }

  if (!status.empty()) {
    ImGui::TextUnformatted(status.c_str());
  }

  ImGui::EndPopup();
}

}  // namespace zedit::frontend
