#pragma once

#include <vector>

#include "zedit/core/mode.hpp"

struct ImGuiIO;

namespace zedit::frontend {

// Reads this frame's keyboard input from ImGuiIO (populated by the GLFW
// backend) and translates it into core KeyEvents. Letting ImGui own the
// GLFW callbacks (rather than installing our own) keeps this a plain,
// once-per-frame poll instead of a callback-ownership fight.
std::vector<zedit::core::KeyEvent> collect_key_events(ImGuiIO& io);

}  // namespace zedit::frontend
