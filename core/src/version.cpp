#include "zedit/core/version.hpp"

#include <string>

namespace zedit::core {

const char* version_string() {
  static const std::string kVersion = std::to_string(kVersionMajor) + "." +
                                       std::to_string(kVersionMinor) + "." +
                                       std::to_string(kVersionPatch);
  return kVersion.c_str();
}

}  // namespace zedit::core
