#pragma once

#include "zedit/core/editor.hpp"

namespace zedit::frontend {

// Renders the top-of-window menu bar (File/Edit/View/Help) via
// ImGui::BeginMainMenuBar, plus the modal popups its "Open..."/"Save
// As..." items trigger (a path-entry text box, since there's no native
// file dialog wired up). Call once per frame.
void render_menu_bar(zedit::core::Editor& ed);

}  // namespace zedit::frontend
