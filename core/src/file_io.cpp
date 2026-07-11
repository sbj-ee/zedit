#include "zedit/core/file_io.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace zedit::core {

void check_file_size_or_throw(uintmax_t size, const std::string& path) {
  if (size > kMaxFileSizeBytes) {
    throw FileTooLargeError("file too large to open (" + std::to_string(size / (1024 * 1024)) +
                             " MB, limit is " +
                             std::to_string(kMaxFileSizeBytes / (1024 * 1024)) + " MB): " + path);
  }
}

std::string read_file(const std::string& path) {
  std::error_code ec;
  uintmax_t size = std::filesystem::file_size(path, ec);
  if (!ec) {
    check_file_size_or_throw(size, path);
  }
  // If file_size() itself failed (nonexistent path, race, ...), fall
  // through to the ifstream open below, which produces the ordinary
  // "failed to open" FileIoError -- that path is unaffected by the size
  // check and keeps its existing "maybe this is a new file" meaning.

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
