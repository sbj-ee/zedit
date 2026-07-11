#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace zedit::core {

class FileIoError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

std::string read_file(const std::string& path);
void write_file(const std::string& path, std::string_view content);

}  // namespace zedit::core
