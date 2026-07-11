#include "status_line.hpp"

#include <imgui.h>

#include <cctype>
#include <string>

namespace zedit::frontend {

using zedit::core::Cursor;
using zedit::core::Editor;
using zedit::core::EditingStyle;
using zedit::core::Mode;

namespace {

// Recomputed fresh each frame from the full buffer text rather than
// maintained incrementally -- simplest correct approach, same tradeoff
// already accepted elsewhere in this codebase (e.g. diff status), fine
// for the modest file sizes this editor targets.
size_t count_words(const std::string& text) {
  size_t count = 0;
  bool in_word = false;
  for (char c : text) {
    bool is_space = std::isspace(static_cast<unsigned char>(c)) != 0;
    if (is_space) {
      in_word = false;
    } else if (!in_word) {
      in_word = true;
      ++count;
    }
  }
  return count;
}

}  // namespace

void render_status_line(const Editor& ed) {
  if (ed.mode() == Mode::CommandLine || ed.mode() == Mode::Search) {
    ImGui::Text("%c%s", ed.line_input_prefix(), ed.command_line_buffer().c_str());
    return;
  }

  // Gedit style has no vim modes to report -- showing "-- INSERT --"
  // permanently (the only mode gedit style is ever actually in, besides
  // a transient selection) would just be noise for someone who chose
  // gedit style specifically to not think about modes. A "-- SELECTION
  // --" indicator during an active selection is still worth showing,
  // since text visibly highlighting without an obvious cause is more
  // confusing than a state label would be.
  bool gedit_style = ed.editing_style() == EditingStyle::Gedit;
  bool has_selection = ed.mode() == Mode::Visual || ed.mode() == Mode::VisualLine;

  std::string mode_prefix;
  if (gedit_style) {
    if (has_selection) {
      mode_prefix = "-- SELECTION --  ";
    }
  } else {
    const char* mode_name = "NORMAL";
    switch (ed.mode()) {
      case Mode::Insert:
        mode_name = "INSERT";
        break;
      case Mode::Visual:
        mode_name = "VISUAL";
        break;
      case Mode::VisualLine:
        mode_name = "VISUAL LINE";
        break;
      default:
        break;
    }
    mode_prefix = std::string("-- ") + mode_name + " --  ";
  }

  Cursor c = ed.cursor();
  size_t total_lines = ed.buffer().line_count();
  size_t words = count_words(ed.buffer().to_string());
  // Position/line-count/word-count come before the path rather than
  // after: they're always short, so keeping them first means they stay
  // visible even when a long path runs past the window's right edge and
  // gets clipped (this window has no horizontal scrolling).
  ImGui::Text("%s%zu:%zu  %zu line%s  %zu word%s  %s%s", mode_prefix.c_str(), c.line + 1,
              c.col + 1, total_lines, total_lines == 1 ? "" : "s", words, words == 1 ? "" : "s",
              ed.filename().empty() ? "[No Name]" : ed.filename().c_str(),
              ed.dirty() ? " [+]" : "");

  if (!ed.last_error().empty()) {
    ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "%s",
                        ed.last_error().c_str());
  }
}

}  // namespace zedit::frontend
