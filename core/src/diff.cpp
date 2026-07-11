#include "zedit/core/diff.hpp"

#include <algorithm>

namespace zedit::core {

DiffResult diff_lines(const std::vector<std::string>& left, const std::vector<std::string>& right) {
  size_t n = left.size();
  size_t m = right.size();

  // dp[i][j] = length of the LCS of left[i..n) and right[j..m).
  std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
  for (size_t i = n; i-- > 0;) {
    for (size_t j = m; j-- > 0;) {
      if (left[i] == right[j]) {
        dp[i][j] = dp[i + 1][j + 1] + 1;
      } else {
        dp[i][j] = std::max(dp[i + 1][j], dp[i][j + 1]);
      }
    }
  }

  DiffResult result;
  result.left.assign(n, DiffLineStatus::Removed);
  result.right.assign(m, DiffLineStatus::Added);

  size_t i = 0;
  size_t j = 0;
  while (i < n && j < m) {
    if (left[i] == right[j]) {
      result.left[i] = DiffLineStatus::Unchanged;
      result.right[j] = DiffLineStatus::Unchanged;
      ++i;
      ++j;
    } else if (dp[i + 1][j] >= dp[i][j + 1]) {
      ++i;
    } else {
      ++j;
    }
  }

  return result;
}

}  // namespace zedit::core
