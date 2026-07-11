#include "menu_bar.hpp"

#include <imgui.h>
#include <unistd.h>

#include <algorithm>
#include <string>

#include "file_dialog.hpp"
#include "file_finder.hpp"
#include "find_replace_dialog.hpp"
#include "recent_files.hpp"
#include "update_checker.hpp"
#include "zedit/core/file_io.hpp"
#include "zedit/core/version.hpp"

namespace zedit::frontend {

using zedit::core::Editor;
using zedit::core::FileIoError;
using zedit::core::FileTooLargeError;
using zedit::core::Key;
using zedit::core::KeyEvent;
using zedit::core::Mode;

namespace {

// Launches the OS's default handler for a URL (xdg-open on Linux, open on
// macOS) without going through a shell -- execlp takes the URL as a plain
// argv entry, not shell-interpreted text, so this is immune to injection
// regardless of what characters end up in it. Deliberately doesn't
// waitpid(): a short-lived launcher process sitting as a zombie until
// zedit exits is harmless for a rare, user-triggered action, and simpler
// than managing its lifecycle for something we don't need to track.
void open_url(const std::string& url) {
  if (url.empty()) {
    return;
  }
#if defined(__APPLE__)
  const char* opener = "open";
#else
  const char* opener = "xdg-open";
#endif
  pid_t pid = fork();
  if (pid == 0) {
    execlp(opener, opener, url.c_str(), static_cast<char*>(nullptr));
    _exit(127);  // only reached if execlp itself failed
  }
}

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

void about_popup(ImTextureID icon_texture) {
  if (!ImGui::BeginPopupModal("About zedit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  if (icon_texture != ImTextureID()) {
    ImGui::Image(icon_texture, ImVec2(80.0f, 80.0f));
    ImGui::SameLine();
  }
  ImGui::BeginGroup();
  ImGui::Text("zedit %s -- a modal text editor", zedit::core::version_string());
  ImGui::Text("gedit-style chrome, neovim-style keyboard-driven editing");
  ImGui::EndGroup();
  ImGui::Separator();
  if (ImGui::Button("Close")) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

}  // namespace

void render_menu_bar(Editor& ed, ImTextureID icon_texture, bool& word_wrap,
                      UpdateChecker& update_checker,
                      std::optional<zedit::core::UpdateInfo>& available_update) {
  bool open_requested = false;
  bool save_as_requested = false;
  bool about_requested = false;
  bool find_replace_requested = false;
  bool find_file_requested = false;
  bool compare_with_requested = false;

  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New")) {
        ed.open_buffer("");
      }
      if (ImGui::MenuItem("Open...")) {
        open_requested = true;
      }
      if (ImGui::MenuItem("Find File...")) {
        find_file_requested = true;
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
      ImGui::Separator();
      // Fuzzy-picks any file in the tree (not just already-open buffers)
      // and diffs it against the current window -- see
      // render_compare_with_popup(); Editor::diff_with() opens the file
      // first if it isn't already a buffer, so this needs no new core
      // logic either.
      if (ImGui::MenuItem("Compare With...")) {
        compare_with_requested = true;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("About zedit")) {
        about_requested = true;
      }
      ImGui::Separator();
      bool checking = update_checker.checking();
      if (ImGui::MenuItem(checking ? "Checking for Updates..." : "Check for Updates", nullptr,
                           false, !checking)) {
        available_update.reset();  // clear a stale notice before the fresh check lands
        update_checker.start_check(zedit::core::version_string());
      }
      if (available_update.has_value()) {
        std::string label = "Update available: " + available_update->version;
        if (ImGui::MenuItem(label.c_str())) {
          open_url(available_update->url);
        }
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
  if (find_file_requested) {
    ImGui::OpenPopup("Find File");
  }
  if (compare_with_requested) {
    ImGui::OpenPopup("Compare With");
  }
  render_open_file_popup(ed);
  render_save_as_popup(ed);
  about_popup(icon_texture);
  render_find_replace_popup(ed);
  render_find_file_popup(ed);
  render_compare_with_popup(ed);
}

}  // namespace zedit::frontend
