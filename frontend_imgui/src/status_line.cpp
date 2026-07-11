#include "status_line.hpp"

#include <imgui.h>

#include <cctype>
#include <string>

namespace zedit::frontend {

using zedit::core::Cursor;
using zedit::core::Editor;
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
  Cursor c = ed.cursor();
  size_t words = count_words(ed.buffer().to_string());
  // Position/word-count come before the path rather than after: they're
  // always short, so keeping them first means they stay visible even when
  // a long path runs past the window's right edge and gets clipped (this
  // window has no horizontal scrolling).
  ImGui::Text("-- %s --  %zu:%zu  %zu word%s  %s%s", mode_name, c.line + 1, c.col + 1, words,
              words == 1 ? "" : "s", ed.filename().empty() ? "[No Name]" : ed.filename().c_str(),
              ed.dirty() ? " [+]" : "");

  if (!ed.last_error().empty()) {
    ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "%s",
                        ed.last_error().c_str());
  }
}

}  // namespace zedit::frontend
