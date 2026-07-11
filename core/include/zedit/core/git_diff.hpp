#pragma once

#include <optional>
#include <string>

namespace zedit::core {

// Fetches `file_path`'s content as of the git HEAD commit, by shelling
// out to `git -C <dir> show HEAD:./<basename>` -- the "./" form resolves
// relative to -C's directory, so this needs no repo-root computation of
// its own. Returns nullopt if the file isn't in a git repo, isn't
// tracked, HEAD doesn't exist yet (a fresh repo with no commits), or git
// isn't installed: all of these just mean "no gutter markers for this
// file," not an error worth surfacing to the user.
std::optional<std::string> git_head_content(const std::string& file_path);

}  // namespace zedit::core
