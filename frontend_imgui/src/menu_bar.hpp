#pragma once

#include <imgui.h>

#include "zedit/core/editor.hpp"

namespace zedit::frontend {

class UpdateUi;

// Renders the top-of-window menu bar (File/Edit/View/Help) via
// ImGui::BeginMainMenuBar, plus the modal popups its "Open..."/"Save
// As..." items trigger (a path-entry text box, since there's no native
// file dialog wired up). Call once per frame. icon_texture is shown in
// the "About zedit" popup; may be 0 if the logo failed to load.
// word_wrap and show_whitespace are toggled in place by the View > Word
// Wrap and View > Show Whitespace checkboxes, respectively.
// The Help menu's update entries are update_ui's (see update_ui.hpp).
void render_menu_bar(zedit::core::Editor& ed, ImTextureID icon_texture, bool& word_wrap,
                      bool& show_whitespace, UpdateUi& update_ui);

}  // namespace zedit::frontend
