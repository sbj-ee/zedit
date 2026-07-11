#pragma once

#include "zedit/core/editor.hpp"

namespace zedit::frontend {

// A row of buttons for the most common actions, sitting below the menu
// bar and above the tab bar. No icon font is loaded, so these are plain
// text buttons rather than icons.
void render_toolbar(zedit::core::Editor& ed);

}  // namespace zedit::frontend
