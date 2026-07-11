#include "file_finder.hpp"

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "zedit/core/fuzzy_match.hpp"

namespace zedit::frontend {

namespace fs = std::filesystem;
using zedit::core::Editor;

namespace {

// Directory names to skip entirely while scanning -- VCS metadata, build
// output, and dependency trees are noise for a "find a file I'm going to
// edit" picker and can be enormous (node_modules, a CMake build dir with
// FetchContent'd sources), so pruning them keeps the scan fast and the
// result list relevant.
bool is_skipped_dir(const std::string& name) {
  return name == ".git" || name == "build" || name == "node_modules" || name == ".cache" ||
         name == "cmake-build-debug" || name == "cmake-build-release";
}

// Caps the scan so an accidental run from something like $HOME doesn't
// hang the popup -- a real project tree is nowhere near this size.
constexpr size_t kMaxFiles = 20000;

std::vector<std::string> scan_files(const fs::path& root) {
  std::vector<std::string> files;
  std::error_code ec;
  fs::recursive_directory_iterator it(
      root, fs::directory_options::skip_permission_denied, ec);
  fs::recursive_directory_iterator end;
  for (; !ec && it != end && files.size() < kMaxFiles; it.increment(ec)) {
    const fs::directory_entry& entry = *it;
    if (entry.is_directory(ec)) {
      if (is_skipped_dir(entry.path().filename().string())) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (entry.is_regular_file(ec)) {
      files.push_back(fs::relative(entry.path(), root, ec).string());
    }
  }
  return files;
}

}  // namespace

void render_find_file_popup(Editor& ed) {
  static bool initialized = false;
  static std::vector<std::string> all_files;
  static std::array<char, 512> query{};
  static std::vector<std::string> filtered;
  static int selected_index = 0;

  if (!ImGui::BeginPopupModal("Find File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    // Reset on close so the next open re-scans instead of reusing a
    // possibly-stale file list from wherever the last session left off.
    initialized = false;
    return;
  }

  if (!initialized) {
    all_files = scan_files(fs::current_path());
    filtered = zedit::core::fuzzy_filter(query.data(), all_files);
    selected_index = 0;
    query[0] = '\0';
    ImGui::SetKeyboardFocusHere();
    initialized = true;
  }

  bool query_changed =
      ImGui::InputText("##find_file_query", query.data(), query.size(),
                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
  if (ImGui::IsItemEdited()) {
    filtered = zedit::core::fuzzy_filter(query.data(), all_files);
    selected_index = 0;
  }

  bool open_selected = query_changed;  // InputText's Enter key returned true

  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !filtered.empty()) {
    selected_index = (selected_index + 1) % static_cast<int>(filtered.size());
  }
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !filtered.empty()) {
    selected_index =
        (selected_index - 1 + static_cast<int>(filtered.size())) % static_cast<int>(filtered.size());
  }

  ImGui::BeginChild("find_file_list", ImVec2(560.0f, 320.0f), true);
  for (size_t i = 0; i < filtered.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    bool is_selected = static_cast<int>(i) == selected_index;
    if (ImGui::Selectable(filtered[i].c_str(), is_selected)) {
      selected_index = static_cast<int>(i);
      open_selected = true;
    }
    if (is_selected) {
      ImGui::SetItemDefaultFocus();
    }
    ImGui::PopID();
  }
  if (filtered.empty()) {
    ImGui::TextDisabled(all_files.empty() ? "(no files found)" : "(no matches)");
  }
  ImGui::EndChild();

  bool cancelled = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);

  if (open_selected && selected_index >= 0 &&
      selected_index < static_cast<int>(filtered.size())) {
    ed.open_buffer(filtered[static_cast<size_t>(selected_index)]);
    cancelled = true;  // reuse the same close path below
  }

  if (cancelled) {
    query[0] = '\0';
    initialized = false;
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

}  // namespace zedit::frontend
