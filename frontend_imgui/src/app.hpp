#pragma once

#include <imgui.h>

#include <optional>
#include <string>
#include <vector>

#include "text_view.hpp"
#include "zedit/core/editor.hpp"
#include "zedit/core/update_check.hpp"

namespace zedit::frontend {

class UpdateChecker;

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

  // update_checker is owned by main() (see update_checker.hpp for why),
  // just polled here once per frame.
  void render_frame(ImGuiIO& io, UpdateChecker& update_checker);
  bool should_close() const { return editor_.should_quit(); }

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
  // Sticky once set: the checker's poll() only reports a found update
  // once (see its own doc comment), so this is what actually persists
  // it across frames for the Help menu to keep showing.
  std::optional<zedit::core::UpdateInfo> available_update_;
};

}  // namespace zedit::frontend
