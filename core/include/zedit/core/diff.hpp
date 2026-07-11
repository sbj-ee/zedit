#pragma once

#include <string>
#include <vector>

namespace zedit::core {

enum class DiffLineStatus { Unchanged, Removed, Added };

struct DiffResult {
  std::vector<DiffLineStatus> left;   // one entry per line in `left`; Unchanged or Removed
  std::vector<DiffLineStatus> right;  // one entry per line in `right`; Unchanged or Added
};

// Line-based diff via a longest-common-subsequence alignment: lines in the
// LCS are Unchanged on both sides; everything else is Removed (left-only)
// or Added (right-only). O(N*M) time and space, which is fine for typical
// source files but not intended for huge ones.
DiffResult diff_lines(const std::vector<std::string>& left, const std::vector<std::string>& right);

}  // namespace zedit::core
