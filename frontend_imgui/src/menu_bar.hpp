#pragma once

#include <imgui.h>

#include <optional>

#include "zedit/core/editor.hpp"
#include "zedit/core/update_check.hpp"

namespace zedit::frontend {

class UpdateChecker;

// Renders the top-of-window menu bar (File/Edit/View/Help) via
// ImGui::BeginMainMenuBar, plus the modal popups its "Open..."/"Save
// As..." items trigger (a path-entry text box, since there's no native
// file dialog wired up). Call once per frame. icon_texture is shown in
// the "About zedit" popup; may be 0 if the logo failed to load.
// word_wrap is toggled in place by the View > Word Wrap checkbox.
// Help > Check for Updates re-triggers update_checker and clears
// available_update so a stale "update available" notice from before a
// fresh check doesn't linger past it.
void render_menu_bar(zedit::core::Editor& ed, ImTextureID icon_texture, bool& word_wrap,
                      UpdateChecker& update_checker,
                      std::optional<zedit::core::UpdateInfo>& available_update);

}  // namespace zedit::frontend
