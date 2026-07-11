#pragma once

#include <string>

namespace zedit::core {

struct RegisterContent {
  std::string text;
  bool linewise = false;
};

}  // namespace zedit::core
