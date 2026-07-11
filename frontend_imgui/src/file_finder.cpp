#include "file_finder.hpp"

#include <imgui.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
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

// Walks upward from `start` looking for a ".git" directory and treats
// that as the project root, falling back to `start` itself if none is
// found by the time we reach the filesystem root. Anchoring the scan
// here -- rather than the process's raw current working directory --
// matters because zedit is often launched from wherever a shell/file
// manager happened to be sitting, not necessarily the project itself;
// scanning the launch cwd verbatim can sweep in an unrelated, enormous
// tree sitting next to or above it (e.g. a Go module cache under
// $HOME/go if zedit was launched from $HOME).
fs::path find_project_root(const fs::path& start) {
  std::error_code ec;
  fs::path canonical = fs::canonical(start, ec);
  if (ec) {
    return start;
  }
  for (fs::path dir = canonical;; dir = dir.parent_path()) {
    if (fs::exists(dir / ".git", ec)) {
      return dir;
    }
    if (dir == dir.parent_path()) {
      break;  // reached the filesystem root without finding one
    }
  }
  return canonical;
}

// Where a picker should scan from: the open buffer's directory (or the
// cwd for an unnamed buffer), walked up to that file's project root.
fs::path scan_root_for(const Editor& ed) {
  fs::path start =
      ed.filename().empty() ? fs::current_path() : fs::path(ed.filename()).parent_path();
  if (start.empty()) {
    start = fs::current_path();
  }
  return find_project_root(start);
}

// Per-instance state for one fuzzy picker popup. Kept as a plain struct
// (rather than function-local statics) so render_find_file_popup and
// render_compare_with_popup -- two separate popups with two separate
// identities -- don't clobber each other's in-progress query/selection by
// sharing statics that a single shared implementation function would
// otherwise have.
struct FinderState {
  bool initialized = false;
  fs::path root;
  std::array<char, 1024> root_input{};  // editable mirror of root, for "cd"-ing elsewhere
  std::vector<std::string> all_files;
  std::array<char, 512> query{};
  std::vector<std::string> filtered;
  int selected_index = 0;
};

void set_root(FinderState& state, fs::path new_root) {
  state.root = std::move(new_root);
  std::snprintf(state.root_input.data(), state.root_input.size(), "%s",
                state.root.string().c_str());
  state.all_files = scan_files(state.root);
  state.filtered = zedit::core::fuzzy_filter(state.query.data(), state.all_files);
  state.selected_index = 0;
}

// Resolves whatever the user typed into the root field into a directory
// to scan: relative input (including "..") is resolved against the
// *current* root rather than the process cwd, so typing ".." reliably
// goes up from wherever the popup is currently looking, and an absolute
// path (e.g. "/home/user/Sync/") jumps anywhere on disk regardless of
// the auto-detected project root -- the project-root default is a
// starting point, not a sandbox.
std::optional<fs::path> resolve_typed_root(const fs::path& current_root, const char* typed) {
  fs::path candidate(typed);
  if (candidate.empty()) {
    return std::nullopt;
  }
  if (candidate.is_relative()) {
    candidate = current_root / candidate;
  }
  std::error_code ec;
  fs::path canonical = fs::canonical(candidate, ec);
  if (ec || !fs::is_directory(canonical, ec) || ec) {
    return std::nullopt;
  }
  return canonical;
}

// Shared picker implementation: scans on open, fuzzy-filters live as you
// type, Enter/click hands the chosen path to `on_select` (open vs.
// diff-with is the only thing that differs between the two popups).
void render_fuzzy_picker(Editor& ed, const char* popup_name, FinderState& state,
                          const std::function<void(Editor&, const std::string&)>& on_select) {
  if (!ImGui::BeginPopupModal(popup_name, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    // Reset on close so the next open re-scans instead of reusing a
    // possibly-stale file list from wherever the last session left off.
    state.initialized = false;
    return;
  }

  bool just_opened = !state.initialized;
  if (!state.initialized) {
    set_root(state, scan_root_for(ed));
    state.query[0] = '\0';
    state.initialized = true;
  }

  ImGui::PushID(popup_name);

  // Editable, not just a display -- the auto-detected project root is a
  // starting point, not a sandbox; typing a different path (absolute,
  // or ".." relative to wherever this is currently looking) and
  // pressing Enter re-scans from there. See resolve_typed_root().
  bool root_confirmed = ImGui::InputText("##root", state.root_input.data(),
                                          state.root_input.size(),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
  if (root_confirmed) {
    if (std::optional<fs::path> resolved =
            resolve_typed_root(state.root, state.root_input.data())) {
      set_root(state, *resolved);
    } else {
      // Invalid input (typo, not a directory, doesn't exist) -- restore
      // the field to the last root that actually worked rather than
      // leaving a dead-end path sitting there.
      std::snprintf(state.root_input.data(), state.root_input.size(), "%s",
                    state.root.string().c_str());
    }
  }
  ImGui::Separator();

  if (just_opened) {
    ImGui::SetKeyboardFocusHere();
  }
  bool query_changed = ImGui::InputText(
      "##query", state.query.data(), state.query.size(),
      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
  if (ImGui::IsItemEdited()) {
    state.filtered = zedit::core::fuzzy_filter(state.query.data(), state.all_files);
    state.selected_index = 0;
  }

  bool confirmed = false;
  if (query_changed) {
    // The query field has initial keyboard focus (below), so typing a
    // target path immediately after opening the popup -- the natural
    // instinct -- lands here, not in the root field above. Try
    // interpreting Enter as "cd" first; only fall through to "open the
    // selected file" if it doesn't resolve to a directory. Confirmed
    // live: without this, typing a path straight into the query field
    // silently did nothing navigable.
    if (std::optional<fs::path> resolved =
            resolve_typed_root(state.root, state.query.data())) {
      state.query[0] = '\0';
      set_root(state, *resolved);
    } else {
      confirmed = true;  // InputText's Enter key returned true
    }
  }

  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !state.filtered.empty()) {
    state.selected_index = (state.selected_index + 1) % static_cast<int>(state.filtered.size());
  }
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !state.filtered.empty()) {
    state.selected_index = (state.selected_index - 1 + static_cast<int>(state.filtered.size())) %
                            static_cast<int>(state.filtered.size());
  }

  ImGui::BeginChild("list", ImVec2(560.0f, 320.0f), true);
  for (size_t i = 0; i < state.filtered.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    bool is_selected = static_cast<int>(i) == state.selected_index;
    if (ImGui::Selectable(state.filtered[i].c_str(), is_selected)) {
      state.selected_index = static_cast<int>(i);
      confirmed = true;
    }
    if (is_selected) {
      ImGui::SetItemDefaultFocus();
    }
    ImGui::PopID();
  }
  if (state.filtered.empty()) {
    ImGui::TextDisabled(state.all_files.empty() ? "(no files found)" : "(no matches)");
  }
  ImGui::EndChild();

  bool cancelled = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);

  if (confirmed && state.selected_index >= 0 &&
      state.selected_index < static_cast<int>(state.filtered.size())) {
    // Absolute, not the root-relative display string -- correctness
    // here can't depend on the process's cwd matching state.root (it
    // usually won't, per find_project_root()'s doc comment).
    std::string absolute =
        (state.root / state.filtered[static_cast<size_t>(state.selected_index)]).string();
    on_select(ed, absolute);
    cancelled = true;  // reuse the same close path below
  }

  if (cancelled) {
    state.query[0] = '\0';
    state.initialized = false;
    ImGui::CloseCurrentPopup();
  }

  ImGui::PopID();
  ImGui::EndPopup();
}

}  // namespace

void render_find_file_popup(Editor& ed) {
  static FinderState state;
  render_fuzzy_picker(ed, "Find File", state,
                       [](Editor& editor, const std::string& path) { editor.open_buffer(path); });
}

void render_compare_with_popup(Editor& ed) {
  static FinderState state;
  render_fuzzy_picker(ed, "Compare With", state,
                       [](Editor& editor, const std::string& path) { editor.diff_with(path); });
}

}  // namespace zedit::frontend
