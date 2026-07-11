#pragma once

#include <string>
#include <vector>

namespace zedit::frontend {

// Recent files are a session-chrome concern (parallel to the window icon
// or theme), not a document-editing one, so this lives in the frontend
// rather than core -- core stays free of filesystem side effects beyond
// the document itself.

// $XDG_CONFIG_HOME/zedit/recent_files, or $HOME/.config/zedit/recent_files.
std::string default_recent_files_path();

// Most-recent-first, capped at a small fixed count. Empty if the file
// doesn't exist yet (no recent files) or the path can't be determined
// (no $HOME).
std::vector<std::string> load_recent_files();

// Moves `path` to the front (adding it if new), persisting the result.
// A no-op if the path can't be determined.
void add_recent_file(const std::string& path);

}  // namespace zedit::frontend
