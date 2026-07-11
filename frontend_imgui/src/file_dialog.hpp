#pragma once

#include "zedit/core/editor.hpp"

namespace zedit::frontend {

// Renders the "Open File" popup's contents: a directory tree browser
// (double-click or Open to pick a file, click a directory to navigate
// into it, ".." to go up) plus a path text field for typing directly.
// Call after ImGui::OpenPopup("Open File"); state (current directory,
// typed path) is self-contained (static) within this file.
void render_open_file_popup(zedit::core::Editor& ed);

}  // namespace zedit::frontend
