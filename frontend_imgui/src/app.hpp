#pragma once

#include <vector>

#include "text_view.hpp"
#include "zedit/core/editor.hpp"

struct ImGuiIO;
struct ImFont;

namespace zedit::frontend {

// Owns the Editor and orchestrates one frame's worth of input handling and
// drawing. GLFW/GL setup stays in main.cpp; this class only touches ImGui.
//
// Renders one TextView pane per open window (:sp/:vsp), arranged stacked or
// side-by-side per Editor::split_layout(). Each pane is rendered by
// temporarily calling Editor::set_current_window(i) and reusing the same
// single-window TextView::render() unmodified -- since Editor's buffer(),
// cursor(), mode(), etc. all resolve through "the current window", this
// needs no separate per-window rendering path.
class App {
 public:
  App(zedit::core::Editor editor, ImFont* font);

  void render_frame(ImGuiIO& io);
  bool should_close() const { return editor_.should_quit(); }

 private:
  zedit::core::Editor editor_;
  std::vector<TextView> text_views_;
  ImFont* font_;
};

}  // namespace zedit::frontend
