#pragma once

#include "zedit/core/editor.hpp"

namespace zedit::frontend {

// A row of buttons for the most common actions. No icon font is loaded,
// so these are plain text buttons rather than icons. Meant to be called
// from within the same BeginMainMenuBar()/EndMainMenuBar() scope as the
// File/Edit/View/Help menus (see menu_bar.cpp), so menus, buttons, and
// tabs all end up on one unified top bar rather than stacked rows.
void render_toolbar(zedit::core::Editor& ed);

// A real drawn vertical line (not a "|" text glyph) for separating groups
// of widgets on the same row -- shared by the toolbar's own button groups
// and by menu_bar.cpp for the gap between the menus and the toolbar.
void draw_vertical_separator();

}  // namespace zedit::frontend
