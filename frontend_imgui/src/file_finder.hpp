#pragma once

#include "zedit/core/editor.hpp"

namespace zedit::frontend {

// Renders the "Find File" popup's contents: a fuzzy-match quick-open,
// like fzf/telescope's file picker. Scans every file under the current
// working directory once when the popup opens (not every frame), then
// filters/sorts that list live via zedit::core::fuzzy_filter as the user
// types. Enter (or a click) opens the selected file. Call after
// ImGui::OpenPopup("Find File"); state is self-contained (static) within
// this file, matching render_open_file_popup's pattern.
void render_find_file_popup(zedit::core::Editor& ed);

}  // namespace zedit::frontend
