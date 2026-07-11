#include "file_dialog.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "zedit/core/file_io.hpp"

namespace zedit::frontend {

namespace fs = std::filesystem;
using zedit::core::Editor;

namespace {

struct DirEntry {
  std::string name;
  bool is_dir;
};

// Unreadable directories (permissions, races with the real filesystem)
// just show up empty rather than crashing the popup.
std::vector<DirEntry> list_directory(const fs::path& dir) {
  std::vector<DirEntry> entries;
  std::error_code ec;
  auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    return entries;
  }
  for (const auto& e : it) {
    entries.push_back(DirEntry{e.path().filename().string(), e.is_directory(ec)});
  }
  std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) {
    if (a.is_dir != b.is_dir) {
      return a.is_dir > b.is_dir;  // directories first
    }
    return a.name < b.name;
  });
  return entries;
}

// Shared tree-browser widget used by both Open and Save As: ".." goes up
// a level, clicking a directory navigates into it (mutating current_dir
// in place), clicking a file reports its bare name. `double_clicked` is
// set when the reported file came from the second click of a
// double-click, so each popup can decide for itself what a double-click
// means (Open: load it now; Save As: use this filename now) without
// this shared widget needing to know either popup's semantics.
std::optional<std::string> render_directory_browser(fs::path& current_dir, bool& double_clicked) {
  std::optional<std::string> picked;
  double_clicked = false;
  ImGui::BeginChild("dir_browser_list", ImVec2(480.0f, 320.0f), true);
  if (current_dir != current_dir.root_path()) {
    if (ImGui::Selectable("..")) {
      current_dir = current_dir.parent_path();
    }
  }
  for (const DirEntry& entry : list_directory(current_dir)) {
    std::string label = entry.is_dir ? entry.name + "/" : entry.name;
    if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
      if (entry.is_dir) {
        current_dir /= entry.name;
      } else {
        picked = entry.name;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          double_clicked = true;
        }
      }
    }
  }
  ImGui::EndChild();
  return picked;
}

// Resolves the directory a popup should start browsing from: an existing
// file's parent directory (canonicalized, so ".." navigation and display
// are consistent), falling back to the cwd for an unnamed buffer or if
// canonicalization fails (e.g. the parent no longer exists).
fs::path starting_directory(const std::string& filename) {
  fs::path start = filename.empty() ? fs::current_path() : fs::path(filename).parent_path();
  if (start.empty()) {
    start = fs::current_path();
  }
  std::error_code ec;
  fs::path canonical = fs::canonical(start, ec);
  return ec ? fs::current_path() : canonical;
}

}  // namespace

void render_open_file_popup(Editor& ed) {
  static fs::path current_dir;
  static std::array<char, 1024> path_buf{};
  static bool initialized = false;
  static std::string error_message;

  if (!ImGui::BeginPopupModal("Open File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    // Not open (or just closed) this frame -- reset so the next time it
    // opens, it re-seeds from the editor's current file instead of
    // resuming wherever the last browse session left off.
    initialized = false;
    return;
  }

  bool just_opened = !initialized;
  if (!initialized) {
    current_dir = starting_directory(ed.filename());
    path_buf[0] = '\0';
    error_message.clear();
    initialized = true;
  }

  ImGui::TextUnformatted(current_dir.string().c_str());
  ImGui::Separator();

  bool double_clicked = false;
  if (std::optional<std::string> picked = render_directory_browser(current_dir, double_clicked)) {
    std::string full = (current_dir / *picked).string();
    std::snprintf(path_buf.data(), path_buf.size(), "%s", full.c_str());
  }
  bool confirmed_by_doubleclick = double_clicked;

  if (just_opened) {
    // Without this, no widget has keyboard focus the first frame the
    // popup is open, so io.WantTextInput stays false and any keys typed
    // right away fall through to the editor underneath instead of this
    // field -- confirmed live: typed text landed in the buffer as vim
    // keystrokes rather than in the Path box. Must be called right
    // before the target widget, not earlier in the frame -- the tree
    // browser's child window has its own separate focus-counter scope,
    // so a call made before it wouldn't reliably reach this InputText.
    ImGui::SetKeyboardFocusHere();
  }
  bool confirmed = ImGui::InputText("Path", path_buf.data(), path_buf.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue);
  confirmed = ImGui::Button("Open") || confirmed || confirmed_by_doubleclick;
  ImGui::SameLine();
  bool cancelled = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);

  if (confirmed && path_buf[0] != '\0') {
    try {
      ed.open_buffer(path_buf.data());
      error_message.clear();
    } catch (const zedit::core::FileTooLargeError& e) {
      // Keep the popup open so the message is actually visible instead
      // of vanishing the instant it appears.
      error_message = e.what();
      confirmed = false;
    }
  }
  if (!error_message.empty()) {
    ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "%s", error_message.c_str());
  }
  if (confirmed || cancelled) {
    path_buf[0] = '\0';
    initialized = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void render_save_as_popup(Editor& ed) {
  static fs::path current_dir;
  static std::array<char, 512> filename_buf{};
  static bool initialized = false;
  static std::string error_message;

  if (!ImGui::BeginPopupModal("Save As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    initialized = false;
    return;
  }

  bool just_opened = !initialized;
  if (!initialized) {
    current_dir = starting_directory(ed.filename());
    std::string base =
        ed.filename().empty() ? std::string() : fs::path(ed.filename()).filename().string();
    std::snprintf(filename_buf.data(), filename_buf.size(), "%s", base.c_str());
    error_message.clear();
    initialized = true;
  }

  ImGui::TextUnformatted(current_dir.string().c_str());
  ImGui::Separator();

  bool double_clicked = false;
  if (std::optional<std::string> picked = render_directory_browser(current_dir, double_clicked)) {
    std::snprintf(filename_buf.data(), filename_buf.size(), "%s", picked->c_str());
  }
  bool confirmed_by_doubleclick = double_clicked;

  if (just_opened) {
    // See the identical comment in render_open_file_popup -- same bug,
    // same fix: focus the field the popup actually opened for, called
    // right before that widget rather than earlier in the frame.
    ImGui::SetKeyboardFocusHere();
  }
  bool confirmed = ImGui::InputText("Filename", filename_buf.data(), filename_buf.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue);
  confirmed = ImGui::Button("Save") || confirmed || confirmed_by_doubleclick;
  ImGui::SameLine();
  bool cancelled = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);

  if (confirmed && filename_buf[0] != '\0') {
    std::string full = (current_dir / filename_buf.data()).string();
    try {
      ed.save_as(full);
      error_message.clear();
    } catch (const zedit::core::FileIoError& e) {
      // Keep the popup open so the message is actually visible instead
      // of vanishing the instant it appears.
      error_message = e.what();
      confirmed = false;
    }
  }
  if (!error_message.empty()) {
    ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "%s", error_message.c_str());
  }
  if (confirmed || cancelled) {
    filename_buf[0] = '\0';
    initialized = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

}  // namespace zedit::frontend
