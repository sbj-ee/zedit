#include "menu_bar.hpp"

#include <imgui.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "command_palette.hpp"
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
  if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
  // Ctrl+Shift+P, guarded so it can't stack a second popup on top of one
  // already open (unlike a MenuItem click, a raw keyboard check isn't
  // blocked by ImGui's own modal-interaction gating).
  bool any_popup_open =
      ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
  bool command_palette_requested =
      !any_popup_open && ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyDown(ImGuiMod_Shift) &&
      ImGui::IsKeyPressed(ImGuiKey_P, false);

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
      // Reuses the exact same key-event path the real
      // Ctrl+C/Ctrl+X/Ctrl+P/Ctrl+A shortcuts go through, rather than
      // duplicating their logic (which lives in ModeStateMachine, not
      // on Editor directly) here.
      if (ImGui::MenuItem("Cut", "Ctrl+X")) {
        ed.handle_key(KeyEvent{Key::CtrlX, 0});
      }
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
      if (ImGui::MenuItem("Command Palette...", "Ctrl+Shift+P")) {
        command_palette_requested = true;
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
      ImGui::Separator();
      // MenuItem flips *p_selected itself and returns true on click, so
      // by the time this branch runs, vi_mode already holds the new
      // target value -- just apply it. Framed as "Vi Mode" (checked =
      // vim's modal grammar, the default) rather than "Gedit Mode" so the
      // checkbox reads naturally either way it's toggled.
      bool vi_mode = (ed.editing_style() == zedit::core::EditingStyle::Vim);
      if (ImGui::MenuItem("Vi Mode", nullptr, &vi_mode)) {
        ed.set_editing_style(vi_mode ? zedit::core::EditingStyle::Vim
                                      : zedit::core::EditingStyle::Gedit);
      }
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
      // Browses the directory tree (see render_compare_with_popup()) to
      // pick any file, not just already-open buffers, and diffs it
      // against the current window; Editor::diff_with() opens the file
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

  if (command_palette_requested) {
    ImGui::OpenPopup("Command Palette");
  }

  // Built fresh each frame (cheap: a few dozen small structs) rather
  // than cached, so a command's captured state (e.g. word_wrap's current
  // value) is never stale. Each entry's action reuses the exact same
  // code path its menu item above already runs -- for the ones that open
  // another popup (Open..., Save As..., Find File..., Compare With...,
  // Find and Replace...), that means setting the same local "requested"
  // bool, letting the "if ([x]_requested) OpenPopup(...)" checks below
  // handle it uniformly rather than duplicating popup-opening logic here.
  // Crucially, this whole block -- and the resulting action() call --
  // must run *before* those checks, not after: a command confirmed this
  // frame needs its "requested" bool set in time for this same frame's
  // check to see it, or the OpenPopup call is missed entirely (the bool
  // gets reset to false at the top of next frame regardless).
  std::vector<Command> commands = {
      {"New", "", [&ed] { ed.open_buffer(""); }},
      {"Open...", "", [&open_requested] { open_requested = true; }},
      {"Find File...", "", [&find_file_requested] { find_file_requested = true; }},
      {"Save", "Ctrl+S",
       [&ed] {
         try {
           ed.save();
         } catch (const FileIoError&) {
         }
       }},
      {"Save As...", "", [&save_as_requested] { save_as_requested = true; }},
      {"Quit", "", [&ed] { ed.request_quit(); }},
      {"Undo", "u", [&ed] { ed.undo(); }},
      {"Redo", "Ctrl+R", [&ed] { ed.redo(); }},
      {"Cut", "Ctrl+X", [&ed] { ed.handle_key(KeyEvent{Key::CtrlX, 0}); }},
      {"Copy", "Ctrl+C", [&ed] { ed.handle_key(KeyEvent{Key::CtrlC, 0}); }},
      {"Paste", "Ctrl+P", [&ed] { ed.handle_key(KeyEvent{Key::CtrlP, 0}); }},
      {"Select All", "Ctrl+A", [&ed] { ed.handle_key(KeyEvent{Key::CtrlA, 0}); }},
      {"Find and Replace...", "",
       [&find_replace_requested] { find_replace_requested = true; }},
      {"Split Horizontal", ":sp", [&ed] { ed.split_horizontal(); }},
      {"Split Vertical", ":vsp", [&ed] { ed.split_vertical(); }},
      {"Close Window", ":close", [&ed] { ed.close_window(); }},
      {"Next Window", "Ctrl+W", [&ed] { ed.next_window(); }},
      {"Toggle Word Wrap", "", [&word_wrap] { word_wrap = !word_wrap; }},
      {"Sort Lines (A-Z)", "", [&ed] { sort_selection_or_all(ed, /*reverse=*/false); }},
      {"Sort Lines (Z-A)", "", [&ed] { sort_selection_or_all(ed, /*reverse=*/true); }},
      {"Compare With...", "",
       [&compare_with_requested] { compare_with_requested = true; }},
      {"About zedit", "", [&about_requested] { about_requested = true; }},
      {"Check for Updates", "",
       [&update_checker, &available_update] {
         available_update.reset();
         update_checker.start_check(zedit::core::version_string());
       }},
  };
  if (std::function<void()> action = render_command_palette_popup(commands)) {
    action();
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
