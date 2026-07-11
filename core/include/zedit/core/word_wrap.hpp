#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace zedit::core {

// A half-open byte range [start, end) within a single line's text.
struct WrapSegment {
  size_t start;
  size_t end;
};

// Splits `line` into segments no wider than `max_chars`, preferring to
// break at the last space before the limit (soft wrap, matching how a
// word processor wraps text) and falling back to a hard break mid-word
// only when a single run has no space to break at (avoids unbounded
// overflow for e.g. a long URL or a minified line). Always returns at
// least one segment, even for an empty line -- callers can rely on
// "one line in, at least one visual row out".
std::vector<WrapSegment> wrap_line(std::string_view line, size_t max_chars);

}  // namespace zedit::core
