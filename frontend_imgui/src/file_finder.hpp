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

// Same fuzzy quick-open, but diffs the selected file against the current
// window (via Editor::diff_with) instead of just opening it -- diff_with
// already opens the file first if it isn't already a buffer, so this
// works on any file in the tree, not just ones already open. Call after
// ImGui::OpenPopup("Compare With"). Independent state from
// render_find_file_popup, so both popups can exist without interfering
// with each other (they're just two instances of the same underlying
// picker aimed at different Editor actions).
void render_compare_with_popup(zedit::core::Editor& ed);

}  // namespace zedit::frontend
