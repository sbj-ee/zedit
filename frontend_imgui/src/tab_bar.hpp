#pragma once

#include "zedit/core/editor.hpp"

namespace zedit::frontend {

// A gedit-style row of buffer tabs. Click to switch; reflects :e/:bn/:bp.
void render_tab_bar(zedit::core::Editor& ed);

}  // namespace zedit::frontend
