#pragma once

#include <functional>
#include <string>
#include <vector>

namespace zedit::frontend {

// One entry in the Command Palette: a display name, an optional shortcut
// label shown alongside it (purely informational -- the shortcut itself,
// if any, is still bound wherever it always was), and the action to run
// when selected. Built fresh each frame by the caller (render_menu_bar),
// which already has every dependency (Editor&, word_wrap, etc.) in scope
// to close over -- the palette itself stays generic, knowing nothing
// about what a command actually does.
struct Command {
  std::string name;
  std::string shortcut;
  std::function<void()> action;
};

// Renders the Command Palette popup's contents: a fuzzy-searchable list
// of `commands`, filtered/sorted live as you type (same matcher as Find
// File), Enter/click to run one. Call after ImGui::OpenPopup("Command
// Palette"). Returns the selected command's action if one was confirmed
// this frame, or an empty std::function otherwise -- deliberately
// returned rather than invoked internally, so the caller can run it
// *after* this popup's own Begin/End block closes (an action like "Open
// File..." opens another popup, which needs to happen at the same
// top-level point every other popup trigger does, not nested inside this
// one's).
std::function<void()> render_command_palette_popup(const std::vector<Command>& commands);

}  // namespace zedit::frontend
