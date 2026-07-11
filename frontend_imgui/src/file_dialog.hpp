#pragma once

#include "zedit/core/editor.hpp"

namespace zedit::frontend {

// Renders the "Open File" popup's contents: a directory tree browser
// (double-click or Open to pick a file, click a directory to navigate
// into it, ".." to go up) plus a path text field for typing directly.
// Call after ImGui::OpenPopup("Open File"); state (current directory,
// typed path) is self-contained (static) within this file.
void render_open_file_popup(zedit::core::Editor& ed);

// Renders the "Save As" popup's contents: the same directory tree
// browser as Open (navigate with the list, ".." to go up) plus a
// filename field -- clicking an existing file in the tree fills it in
// (so overwriting is a click away), double-clicking or Save confirms.
// Starts browsing from the current buffer's directory (or the cwd for
// an unnamed buffer), pre-filled with its existing filename if it has
// one. Call after ImGui::OpenPopup("Save As").
void render_save_as_popup(zedit::core::Editor& ed);

}  // namespace zedit::frontend
