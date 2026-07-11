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

  return events;
}

}  // namespace zedit::frontend
