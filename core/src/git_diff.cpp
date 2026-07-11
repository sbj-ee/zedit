#include "zedit/core/git_diff.hpp"

#include <filesystem>

#include "zedit/core/subprocess.hpp"

namespace zedit::core {

namespace fs = std::filesystem;

std::optional<std::string> git_head_content(const std::string& file_path) {
  fs::path path(file_path);
  std::string basename = path.filename().string();
  if (basename.empty()) {
    return std::nullopt;
  }
  std::string dir = path.parent_path().empty() ? "." : path.parent_path().string();

  Subprocess proc;
  if (!proc.start("git", {"-C", dir, "show", "HEAD:./" + basename})) {
    return std::nullopt;
  }
  std::string output;
  std::string chunk;
  while (!(chunk = proc.read_some()).empty()) {
    output += chunk;
  }
  // proc's destructor (RAII) reaps the child -- no explicit stop() needed,
  // matching how UpdateChecker's Subprocess is used the same way.
  return output.empty() ? std::nullopt : std::make_optional(std::move(output));
}

}  // namespace zedit::core
