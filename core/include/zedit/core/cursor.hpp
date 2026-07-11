#pragma once

#include <cstddef>

namespace zedit::core {

struct Cursor {
  size_t line = 0;
  size_t col = 0;
};

}  // namespace zedit::core
