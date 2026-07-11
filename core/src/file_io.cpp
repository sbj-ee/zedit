#include "zedit/core/file_io.hpp"

#include <fstream>
#include <sstream>

namespace zedit::core {

std::string read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw FileIoError("failed to open file for reading: " + path);
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

void write_file(const std::string& path, std::string_view content) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    throw FileIoError("failed to open file for writing: " + path);
  }
  file.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!file) {
    throw FileIoError("failed to write file: " + path);
  }
}

}  // namespace zedit::core
