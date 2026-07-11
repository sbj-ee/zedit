#include "menu_bar.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>

#include "file_dialog.hpp"
#include "find_replace_dialog.hpp"
#include "recent_files.hpp"
#include "zedit/core/file_io.hpp"

namespace zedit::frontend {

using zedit::core::Editor;
using zedit::core::FileIoError;
using zedit::core::FileTooLargeError;
using zedit::core::Key;
using zedit::core::KeyEvent;
using zedit::core::Mode;

namespace {

// Sorts the current Visual/Visual Line selection's lines if one is
// active, otherwise the whole buffer -- matches gedit's own Tools >
// Sort, which operates on the selection when there is one.
void sort_selection_or_all(Editor& ed, bool reverse) {
  size_t total = ed.buffer().line_count();
  size_t start = 0;
  size_t end = total > 0 ? total - 1 : 0;
  if (ed.mode() == Mode::Visual || ed.mode() == Mode::VisualLine) {
    start = std::min(ed.visual_anchor().line, ed.cursor().line);
    end = std::max(ed.visual_anchor().line, ed.cursor().line);
  }
  ed.sort_lines(start, end, reverse);
}

// Popup path-entry buffers are static (one editor, one menu bar, one
// popup open at a time) rather than App members -- keeps the popup
// self-contained here instead of threading text-input state through
// App's public surface for something only this file touches.
void save_as_popup(Editor& ed) {
  static std::array<char, 512> path{};
  if (!ImGui::BeginPopupModal("Save As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  bool confirmed = ImGui::InputText("Path", path.data(), path.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue);
  confirmed = ImGui::Button("Save") || confirmed;
  ImGui::SameLine();
  bool cancelled = ImGui::Button("Cancel");
  if (confirmed && path[0] != '\0') {
    try {
      ed.save_as(path.data());
    } catch (const FileIoError&) {
      // Nothing more actionable to do from a modal popup than leave the
      // buffer dirty -- the status line already surfaces save failures
      // for the ":w"/Ctrl-S paths, but this popup has no such display.
    }
  }
  if (confirmed || cancelled) {
    path[0] = '\0';
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void about_popup(ImTextureID icon_texture) {
  if (!ImGui::BeginPopupModal("About zedit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  if (icon_texture != ImTextureID()) {
    ImGui::Image(icon_texture, ImVec2(80.0f, 80.0f));
    ImGui::SameLine();
  }
  ImGui::BeginGroup();
  ImGui::Text("zedit -- a modal text editor");
  ImGui::Text("gedit-style chrome, neovim-style keyboard-driven editing");
  ImGui::EndGroup();
  ImGui::Separator();
  if (ImGui::Button("Close")) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

}  // namespace

void render_menu_bar(Editor& ed, ImTextureID icon_texture, bool& word_wrap) {
  bool open_requested = false;
  bool save_as_requested = false;
  bool about_requested = false;
  bool find_replace_requested = false;

  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New")) {
        ed.open_buffer("");
      }
      if (ImGui::MenuItem("Open...")) {
        open_requested = true;
      }
      if (ImGui::BeginMenu("Recent Files")) {
        std::vector<std::string> recents = load_recent_files();
        if (recents.empty()) {
          ImGui::TextDisabled("(none)");
        } else {
          for (const std::string& path : recents) {
            if (ImGui::MenuItem(path.c_str())) {
              try {
                ed.open_buffer(path);
              } catch (const FileTooLargeError&) {
                // No error-display surface on a plain menu item -- the
                // status line/last_error() path (used by :e) isn't
                // reachable from here, so this just silently declines.
              }
            }
          }
        }
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Save", "Ctrl+S")) {
        try {
          ed.save();
        } catch (const FileIoError&) {
        }
      }
      if (ImGui::MenuItem("Save As...")) {
        save_as_requested = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Quit")) {
        ed.request_quit();
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "u")) {
        ed.undo();
      }
      if (ImGui::MenuItem("Redo", "Ctrl+R")) {
        ed.redo();
      }
      ImGui::Separator();
      // Reuses the exact same key-event path the real Ctrl+C/Ctrl+P/Ctrl+A
      // shortcuts go through, rather than duplicating their logic (which
      // lives in ModeStateMachine, not on Editor directly) here.
      if (ImGui::MenuItem("Copy", "Ctrl+C")) {
        ed.handle_key(KeyEvent{Key::CtrlC, 0});
      }
      if (ImGui::MenuItem("Paste", "Ctrl+P")) {
        ed.handle_key(KeyEvent{Key::CtrlP, 0});
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Select All", "Ctrl+A")) {
        ed.handle_key(KeyEvent{Key::CtrlA, 0});
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Find and Replace...")) {
        find_replace_requested = true;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      if (ImGui::MenuItem("Split Horizontal", ":sp")) {
        ed.split_horizontal();
      }
      if (ImGui::MenuItem("Split Vertical", ":vsp")) {
        ed.split_vertical();
      }
      if (ImGui::MenuItem("Close Window", ":close")) {
        ed.close_window();
      }
      if (ImGui::MenuItem("Next Window", "Ctrl+W")) {
        ed.next_window();
      }
      ImGui::Separator();
      ImGui::MenuItem("Word Wrap", nullptr, &word_wrap);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
      // Sorts the active Visual/Visual Line selection's lines if there is
      // one, otherwise the whole buffer -- see sort_selection_or_all().
      if (ImGui::MenuItem("Sort Lines (A-Z)")) {
        sort_selection_or_all(ed, /*reverse=*/false);
      }
      if (ImGui::MenuItem("Sort Lines (Z-A)")) {
        sort_selection_or_all(ed, /*reverse=*/true);
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("About zedit")) {
        about_requested = true;
      }
      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
  }

  if (open_requested) {
    ImGui::OpenPopup("Open File");
  }
  if (save_as_requested) {
    ImGui::OpenPopup("Save As");
  }
  if (about_requested) {
    ImGui::OpenPopup("About zedit");
  }
  if (find_replace_requested) {
    ImGui::OpenPopup("Find and Replace");
  }
  render_open_file_popup(ed);
  save_as_popup(ed);
  about_popup(icon_texture);
  render_find_replace_popup(ed);
}

}  // namespace zedit::frontend
