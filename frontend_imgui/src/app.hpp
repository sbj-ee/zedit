#pragma once

#include "text_view.hpp"
#include "zedit/core/editor.hpp"

struct ImGuiIO;
struct ImFont;

namespace zedit::frontend {

// Owns the Editor and orchestrates one frame's worth of input handling and
// drawing. GLFW/GL setup stays in main.cpp; this class only touches ImGui.
class App {
 public:
  App(zedit::core::Editor editor, ImFont* font);

  void render_frame(ImGuiIO& io);
  bool should_close() const { return editor_.should_quit(); }

 private:
  zedit::core::Editor editor_;
  TextView text_view_;
  ImFont* font_;
};

}  // namespace zedit::frontend
