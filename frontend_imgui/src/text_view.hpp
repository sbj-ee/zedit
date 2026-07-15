#pragma once

#include <imgui.h>

#include <cstddef>

#include "zedit/core/editor.hpp"

namespace zedit::frontend {

// Custom-drawn buffer canvas. Only ever queries the buffer for the lines
// currently in the viewport, so cost per frame is O(visible lines), not
// O(file size), regardless of how large the open file is.
class TextView {
 public:
  // width == 0 means "fill available width" (the single-pane/stacked-split
  // case); an explicit width is used for side-by-side panes. word_wrap and
  // show_whitespace apply uniformly to every pane (there's one toggle each,
  // not one per window). Returns true if the pane was clicked this frame,
  // so callers can give it focus.
  bool render(zedit::core::Editor& ed, ImFont* font, float height, float width = 0.0f,
              bool word_wrap = false, bool show_whitespace = false);

  // Screen-space top-left of the cursor glyph as of the last render() call,
  // for positioning overlays (e.g. the hover popup) relative to it.
  ImVec2 cursor_screen_pos() const { return cursor_screen_pos_; }

 private:
  size_t first_visible_line_ = 0;
  ImVec2 cursor_screen_pos_{0.0f, 0.0f};
  // SIZE_MAX sentinel means "never rendered yet" -- forces the initial
  // scroll-to-cursor on the first frame. Tracked so auto-scroll only
  // re-snaps to the cursor when it actually moved, not every frame;
  // otherwise a mouse-wheel scroll away from the cursor would get undone
  // on the very next frame.
  size_t last_cursor_line_ = static_cast<size_t>(-1);

  void scroll_to_keep_cursor_visible(const zedit::core::Editor& ed,
                                      size_t visible_lines);
};

}  // namespace zedit::frontend
