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
// or Added (right-only). Myers' algorithm with common prefix/suffix
// trimming: O((N+M)·D) time for D differing lines, linear space.
DiffResult diff_lines(const std::vector<std::string>& left, const std::vector<std::string>& right);

}  // namespace zedit::core
