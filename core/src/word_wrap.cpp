#include "zedit/core/word_wrap.hpp"

namespace zedit::core {

std::vector<WrapSegment> wrap_line(std::string_view line, size_t max_chars) {
  std::vector<WrapSegment> segments;
  if (max_chars == 0) {
    segments.push_back(WrapSegment{0, line.size()});
    return segments;
  }

  size_t pos = 0;
  while (true) {
    size_t remaining = line.size() - pos;
    if (remaining <= max_chars) {
      segments.push_back(WrapSegment{pos, line.size()});
      break;
    }
    size_t limit = pos + max_chars;
    size_t break_at = line.rfind(' ', limit - 1);
    if (break_at == std::string_view::npos || break_at < pos) {
      // No space within this segment's own window -- hard break.
      segments.push_back(WrapSegment{pos, limit});
      pos = limit;
    } else {
      segments.push_back(WrapSegment{pos, break_at});
      pos = break_at + 1;  // skip the space itself
    }
  }
  return segments;
}

}  // namespace zedit::core
