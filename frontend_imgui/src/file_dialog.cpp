#include "file_dialog.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
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

  if (!initialized) {
    fs::path start =
        ed.filename().empty() ? fs::current_path() : fs::path(ed.filename()).parent_path();
    if (start.empty()) {
      start = fs::current_path();
    }
    std::error_code ec;
    fs::path canonical = fs::canonical(start, ec);
    current_dir = ec ? fs::current_path() : canonical;
    path_buf[0] = '\0';
    error_message.clear();
    initialized = true;
  }

  ImGui::TextUnformatted(current_dir.string().c_str());
  ImGui::Separator();

  bool confirmed_by_doubleclick = false;

  ImGui::BeginChild("file_dialog_list", ImVec2(480.0f, 320.0f), true);
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
        std::string full = (current_dir / entry.name).string();
        std::snprintf(path_buf.data(), path_buf.size(), "%s", full.c_str());
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          confirmed_by_doubleclick = true;
        }
      }
    }
  }
  ImGui::EndChild();

  bool confirmed = ImGui::InputText("Path", path_buf.data(), path_buf.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue);
  confirmed = ImGui::Button("Open") || confirmed || confirmed_by_doubleclick;
  ImGui::SameLine();
  bool cancelled = ImGui::Button("Cancel");

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

}  // namespace zedit::frontend
