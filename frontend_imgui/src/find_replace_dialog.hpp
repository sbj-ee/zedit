#pragma once

#include "zedit/core/editor.hpp"

namespace zedit::frontend {

// Renders the "Find and Replace" popup's contents: Find/Replace fields
// plus Find Next / Replace / Replace All buttons, gedit-style. Call after
// ImGui::OpenPopup("Find and Replace"); state (typed text) is
// self-contained (static) within this file.
void render_find_replace_popup(zedit::core::Editor& ed);

}  // namespace zedit::frontend
