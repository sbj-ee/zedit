#include "input_map.hpp"

#include <imgui.h>

namespace zedit::frontend {

std::vector<zedit::core::KeyEvent> collect_key_events(ImGuiIO& io) {
  using zedit::core::Key;
  using zedit::core::KeyEvent;

  std::vector<KeyEvent> events;

  for (ImWchar c : io.InputQueueCharacters) {
    if (c > 0 && c < 128) {
      events.push_back(KeyEvent{Key::Char, static_cast<char>(c)});
    }
  }

  auto add_if_pressed = [&](ImGuiKey key, Key mapped) {
    if (ImGui::IsKeyPressed(key, true)) {
      events.push_back(KeyEvent{mapped, 0});
    }
  };
  add_if_pressed(ImGuiKey_Enter, Key::Enter);
  add_if_pressed(ImGuiKey_KeypadEnter, Key::Enter);
  add_if_pressed(ImGuiKey_Escape, Key::Escape);
  add_if_pressed(ImGuiKey_Backspace, Key::Backspace);
  add_if_pressed(ImGuiKey_Tab, Key::Tab);
  add_if_pressed(ImGuiKey_LeftArrow, Key::Left);
  add_if_pressed(ImGuiKey_RightArrow, Key::Right);
  add_if_pressed(ImGuiKey_UpArrow, Key::Up);
  add_if_pressed(ImGuiKey_DownArrow, Key::Down);

  // Ctrl+<letter> combos don't produce InputQueueCharacters entries (GLFW
  // suppresses char callbacks while a modifier is held), so they're only
  // reachable via this explicit modifier + key-press check.
  bool ctrl_held = ImGui::IsKeyDown(ImGuiMod_Ctrl);
  if (ctrl_held && ImGui::IsKeyPressed(ImGuiKey_R, true)) {
    events.push_back(KeyEvent{Key::CtrlR, 0});
  }
  if (ctrl_held && ImGui::IsKeyPressed(ImGuiKey_W, true)) {
    events.push_back(KeyEvent{Key::CtrlW, 0});
  }
  if (ctrl_held && ImGui::IsKeyPressed(ImGuiKey_A, true)) {
    events.push_back(KeyEvent{Key::CtrlA, 0});
  }
  if (ctrl_held && ImGui::IsKeyPressed(ImGuiKey_P, true)) {
    events.push_back(KeyEvent{Key::CtrlP, 0});
  }
  if (ctrl_held && ImGui::IsKeyPressed(ImGuiKey_C, true)) {
    events.push_back(KeyEvent{Key::CtrlC, 0});
  }
  if (ctrl_held && ImGui::IsKeyPressed(ImGuiKey_S, true)) {
    events.push_back(KeyEvent{Key::CtrlS, 0});
  }

  return events;
}

}  // namespace zedit::frontend
