#pragma once

#include <imgui.h>

#include <string>
#include <vector>

#include "text_view.hpp"
#include "zedit/core/editor.hpp"

namespace zedit::frontend {

class UpdateUi;

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
  // icon_texture may be 0 (ImTextureID's null value) if the logo failed to
  // load as a GL texture -- the About popup just skips drawing it then.
  App(zedit::core::Editor editor, ImFont* font, ImTextureID icon_texture);

  // update_ui is owned by main() (see update_ui.hpp for why), just driven
  // here once per frame.
  void render_frame(ImGuiIO& io, UpdateUi& update_ui);
  // The current file's name, for reopening it after an update restart.
  const std::string& current_filename() const { return editor_.filename(); }
  bool should_close() const { return editor_.should_quit(); }
  // The window close button and Cmd-Q: quits at once if every buffer is
  // clean, otherwise opens the "Unsaved changes" prompt.
  void ask_quit() { editor_.ask_quit(); }

 private:
  zedit::core::Editor editor_;
  std::vector<TextView> text_views_;
  ImFont* font_;
  ImTextureID icon_texture_;
  // Tracks the last filename recorded into the recent-files list, so a
  // change is detected (and recorded) uniformly regardless of how it
  // happened -- :e, the Open dialog, Save As, or the initial CLI arg --
  // rather than needing a call to add_recent_file() at every one of
  // those sites individually.
  std::string last_recorded_filename_;
  // One toggle for every pane, not per-window -- see View > Word Wrap.
  bool word_wrap_ = false;
  // One toggle for every pane, not per-window -- see View > Show Whitespace.
  bool show_whitespace_ = false;
  // Latches OpenPopup for the recovery modal so we only open once
  // per pending offer (ImGui needs OpenPopup on the frame before/
  // of BeginPopupModal).
  bool recovery_popup_opened_ = false;
  // The quit prompt can't open the Save As dialog from inside itself (a
  // popup opened there would be its child, but Save As is rendered at the
  // top level), so it sets this, closes, and the next frame opens Save As.
  bool quit_save_as_requested_ = false;

  void render_quit_prompt();
};

}  // namespace zedit::frontend
