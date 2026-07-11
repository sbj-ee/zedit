#pragma once

#include <imgui.h>

#include "zedit/core/editor.hpp"

namespace zedit::frontend {

// Renders the top-of-window menu bar (File/Edit/View/Help) via
// ImGui::BeginMainMenuBar, plus the modal popups its "Open..."/"Save
// As..." items trigger (a path-entry text box, since there's no native
// file dialog wired up). Call once per frame. icon_texture is shown in
// the "About zedit" popup; may be 0 if the logo failed to load.
// word_wrap is toggled in place by the View > Word Wrap checkbox.
void render_menu_bar(zedit::core::Editor& ed, ImTextureID icon_texture, bool& word_wrap);

}  // namespace zedit::frontend
