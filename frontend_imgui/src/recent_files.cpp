#include "recent_files.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace zedit::frontend {

namespace {
constexpr size_t kMaxRecentFiles = 10;
}  // namespace

std::string default_recent_files_path() {
  std::string base;
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
    base = xdg;
  } else if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    base = std::string(home) + "/.config";
  }
  if (base.empty()) {
    return "";
  }
  return base + "/zedit/recent_files";
}

std::vector<std::string> load_recent_files() {
  std::vector<std::string> result;
  std::string path = default_recent_files_path();
  if (path.empty()) {
    return result;
  }
  std::ifstream file(path);
  if (!file) {
    return result;
  }
  std::string line;
  while (result.size() < kMaxRecentFiles && std::getline(file, line)) {
    if (!line.empty()) {
      result.push_back(line);
    }
  }
  return result;
}

void add_recent_file(const std::string& path) {
  if (path.empty()) {
    return;
  }
  std::string list_path = default_recent_files_path();
  if (list_path.empty()) {
    return;
  }

  std::vector<std::string> files = load_recent_files();
  files.erase(std::remove(files.begin(), files.end(), path), files.end());
  files.insert(files.begin(), path);
  if (files.size() > kMaxRecentFiles) {
    files.resize(kMaxRecentFiles);
  }

  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(list_path).parent_path(), ec);

  std::ofstream out(list_path, std::ios::trunc);
  if (!out) {
    return;
  }
  for (const std::string& f : files) {
    out << f << "\n";
  }
}

}  // namespace zedit::frontend
