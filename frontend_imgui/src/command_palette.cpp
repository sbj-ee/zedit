#include "command_palette.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <vector>

#include "zedit/core/fuzzy_match.hpp"

namespace zedit::frontend {

namespace {

struct PaletteState {
  bool initialized = false;
  std::array<char, 256> query{};
  std::vector<size_t> filtered;  // indices into the caller's `commands`
  int selected_index = 0;
};

// Scores every command's name against the query and keeps the matches,
// best first -- same algorithm as zedit::core::fuzzy_filter, but
// index-preserving (fuzzy_filter itself returns matched strings, losing
// the association back to each Command's action) so results can still be
// mapped back to `commands` after sorting.
void refilter(const std::vector<Command>& commands, PaletteState& state) {
  std::vector<std::pair<int, size_t>> scored;
  scored.reserve(commands.size());
  for (size_t i = 0; i < commands.size(); ++i) {
    if (std::optional<int> score = zedit::core::fuzzy_score(state.query.data(), commands[i].name)) {
      scored.emplace_back(*score, i);
    }
  }
  std::stable_sort(scored.begin(), scored.end(),
                    [](const auto& a, const auto& b) { return a.first > b.first; });
  state.filtered.clear();
  state.filtered.reserve(scored.size());
  for (const auto& [score, index] : scored) {
    state.filtered.push_back(index);
  }
}

}  // namespace

std::function<void()> render_command_palette_popup(const std::vector<Command>& commands) {
  static PaletteState state;

  if (!ImGui::BeginPopupModal("Command Palette", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    // Not open (or just closed) this frame -- reset so the next open
    // re-seeds from scratch instead of resuming stale filter state.
    state.initialized = false;
    return {};
  }

  bool just_opened = !state.initialized;
  if (!state.initialized) {
    state.query[0] = '\0';
    state.selected_index = 0;
    refilter(commands, state);
    state.initialized = true;
  }

  if (just_opened) {
    ImGui::SetKeyboardFocusHere();
  }
  bool query_changed = ImGui::InputText(
      "##query", state.query.data(), state.query.size(),
      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
  if (ImGui::IsItemEdited()) {
    refilter(commands, state);
    state.selected_index = 0;
  }

  bool confirmed = query_changed;  // InputText's Enter key returned true

  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !state.filtered.empty()) {
    state.selected_index = (state.selected_index + 1) % static_cast<int>(state.filtered.size());
  }
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !state.filtered.empty()) {
    state.selected_index = (state.selected_index - 1 + static_cast<int>(state.filtered.size())) %
                            static_cast<int>(state.filtered.size());
  }

  ImGui::BeginChild("list", ImVec2(420.0f, 320.0f), true);
  for (size_t row = 0; row < state.filtered.size(); ++row) {
    const Command& cmd = commands[state.filtered[row]];
    ImGui::PushID(static_cast<int>(row));
    bool is_selected = static_cast<int>(row) == state.selected_index;
    if (ImGui::Selectable(cmd.name.c_str(), is_selected)) {
      state.selected_index = static_cast<int>(row);
      confirmed = true;
    }
    if (!cmd.shortcut.empty()) {
      ImGui::SameLine(300.0f);
      ImGui::TextDisabled("%s", cmd.shortcut.c_str());
    }
    if (is_selected) {
      ImGui::SetItemDefaultFocus();
    }
    ImGui::PopID();
  }
  if (state.filtered.empty()) {
    ImGui::TextDisabled("(no matching commands)");
  }
  ImGui::EndChild();

  bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);

  std::function<void()> result;
  if (confirmed && state.selected_index >= 0 &&
      state.selected_index < static_cast<int>(state.filtered.size())) {
    result = commands[state.filtered[static_cast<size_t>(state.selected_index)]].action;
    cancelled = true;  // reuse the same close path below
  }

  if (cancelled) {
    state.query[0] = '\0';
    state.initialized = false;
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
  return result;
}

}  // namespace zedit::frontend
