#include "zedit/core/diff.hpp"

#include <string_view>
#include <unordered_map>

namespace zedit::core {

namespace {

// Myers' O((N+M)·D) diff, linear-space "middle snake" form (the bisect
// used by diff-match-patch), over lines interned to integers. The common
// prefix and suffix are trimmed at every level first, so the usual case --
// a few edited lines in a big file -- costs little more than one pass.
class MyersDiff {
 public:
  MyersDiff(std::vector<int> a, std::vector<int> b, DiffResult& out)
      : a_(std::move(a)), b_(std::move(b)), out_(out) {}

  void run() { diff(0, static_cast<int>(a_.size()), 0, static_cast<int>(b_.size())); }

 private:
  std::vector<int> a_;
  std::vector<int> b_;
  DiffResult& out_;

  void match(int i, int j) {
    out_.left[static_cast<size_t>(i)] = DiffLineStatus::Unchanged;
    out_.right[static_cast<size_t>(j)] = DiffLineStatus::Unchanged;
  }

  void diff(int a_lo, int a_hi, int b_lo, int b_hi) {
    while (a_lo < a_hi && b_lo < b_hi && a_[a_lo] == b_[b_lo]) {
      match(a_lo++, b_lo++);
    }
    while (a_lo < a_hi && b_lo < b_hi && a_[a_hi - 1] == b_[b_hi - 1]) {
      match(--a_hi, --b_hi);
    }
    if (a_lo == a_hi || b_lo == b_hi) {
      return;  // pure insertion or deletion: already marked Added/Removed
    }
    int x = 0;
    int y = 0;
    if (!bisect(a_lo, a_hi, b_lo, b_hi, x, y)) {
      return;  // nothing in common
    }
    diff(a_lo, a_lo + x, b_lo, b_lo + y);
    diff(a_lo + x, a_hi, b_lo + y, b_hi);
  }

  // Finds a point on an optimal path through a[a_lo,a_hi) × b[b_lo,b_hi)
  // where the forward and reverse searches meet. Returns false if the
  // ranges share nothing.
  bool bisect(int a_lo, int a_hi, int b_lo, int b_hi, int& split_x, int& split_y) const {
    const int n = a_hi - a_lo;
    const int m = b_hi - b_lo;
    const int* A = a_.data() + a_lo;
    const int* B = b_.data() + b_lo;
    const int max_d = (n + m + 1) / 2;
    const int v_offset = max_d;
    const int v_length = 2 * max_d + 2;
    std::vector<int> v1(static_cast<size_t>(v_length), -1);
    std::vector<int> v2(static_cast<size_t>(v_length), -1);
    v1[static_cast<size_t>(v_offset + 1)] = 0;
    v2[static_cast<size_t>(v_offset + 1)] = 0;
    const int delta = n - m;
    const bool front = (delta % 2) != 0;
    int k1start = 0, k1end = 0, k2start = 0, k2end = 0;
    auto at = [](std::vector<int>& v, int i) -> int& { return v[static_cast<size_t>(i)]; };

    for (int d = 0; d < max_d; ++d) {
      for (int k1 = -d + k1start; k1 <= d - k1end; k1 += 2) {
        const int k1_offset = v_offset + k1;
        int x1;
        if (k1 == -d || (k1 != d && at(v1, k1_offset - 1) < at(v1, k1_offset + 1))) {
          x1 = at(v1, k1_offset + 1);
        } else {
          x1 = at(v1, k1_offset - 1) + 1;
        }
        int y1 = x1 - k1;
        while (x1 < n && y1 < m && A[x1] == B[y1]) {
          ++x1;
          ++y1;
        }
        at(v1, k1_offset) = x1;
        if (x1 > n) {
          k1end += 2;
        } else if (y1 > m) {
          k1start += 2;
        } else if (front) {
          const int k2_offset = v_offset + delta - k1;
          if (k2_offset >= 0 && k2_offset < v_length && at(v2, k2_offset) != -1) {
            const int x2 = n - at(v2, k2_offset);
            if (x1 >= x2) {
              split_x = x1;
              split_y = y1;
              return true;
            }
          }
        }
      }
      for (int k2 = -d + k2start; k2 <= d - k2end; k2 += 2) {
        const int k2_offset = v_offset + k2;
        int x2;
        if (k2 == -d || (k2 != d && at(v2, k2_offset - 1) < at(v2, k2_offset + 1))) {
          x2 = at(v2, k2_offset + 1);
        } else {
          x2 = at(v2, k2_offset - 1) + 1;
        }
        int y2 = x2 - k2;
        while (x2 < n && y2 < m && A[n - x2 - 1] == B[m - y2 - 1]) {
          ++x2;
          ++y2;
        }
        at(v2, k2_offset) = x2;
        if (x2 > n) {
          k2end += 2;
        } else if (y2 > m) {
          k2start += 2;
        } else if (!front) {
          const int k1_offset = v_offset + delta - k2;
          if (k1_offset >= 0 && k1_offset < v_length && at(v1, k1_offset) != -1) {
            const int x1 = at(v1, k1_offset);
            const int y1 = v_offset + x1 - k1_offset;
            if (x1 >= n - x2) {
              split_x = x1;
              split_y = y1;
              return true;
            }
          }
        }
      }
    }
    return false;
  }
};

}  // namespace

DiffResult diff_lines(const std::vector<std::string>& left, const std::vector<std::string>& right) {
  DiffResult result;
  result.left.assign(left.size(), DiffLineStatus::Removed);
  result.right.assign(right.size(), DiffLineStatus::Added);

  // Intern lines so the inner loops compare ints, not strings.
  std::unordered_map<std::string_view, int> ids;
  ids.reserve(left.size() + right.size());
  auto intern = [&](const std::vector<std::string>& lines) {
    std::vector<int> out;
    out.reserve(lines.size());
    for (const std::string& line : lines) {
      out.push_back(ids.emplace(line, static_cast<int>(ids.size())).first->second);
    }
    return out;
  };
  std::vector<int> a = intern(left);
  std::vector<int> b = intern(right);
  MyersDiff(std::move(a), std::move(b), result).run();
  return result;
}

}  // namespace zedit::core
