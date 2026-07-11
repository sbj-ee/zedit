#include "zedit/core/fuzzy_match.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <vector>

namespace zedit::core {

namespace {

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool is_separator(char c) { return c == '/' || c == '_' || c == '-' || c == '.'; }

constexpr int kNegInf = std::numeric_limits<int>::min() / 2;
constexpr int kMatchBase = 1;
constexpr int kBoundaryBonus = 10;
constexpr int kConsecutiveBonus = 5;

int boundary_bonus(std::string_view candidate, size_t j) {
  bool at_start = j == 0;
  bool after_separator = j > 0 && is_separator(candidate[j - 1]);
  return (at_start || after_separator) ? kBoundaryBonus : 0;
}

}  // namespace

// Standard fzy/fzf-style subsequence DP: for each prefix of `query`, track
// the best score achievable (a) ending with an exact match at each
// candidate position (`match_at`), and (b) using any alignment up to and
// including that position (`best_upto`, a running max). This finds the
// globally best-scoring alignment rather than greedily taking the first
// occurrence of each query character -- e.g. for query "ed" against
// "core/editor.hpp", it correctly prefers the 'e' right after the '/'
// over the earlier 'e' in "core", since that alignment scores higher
// overall (boundary bonus, and 'd' then falls out as consecutive too).
std::optional<int> fuzzy_score(std::string_view query, std::string_view candidate) {
  if (query.empty()) {
    return 0;
  }
  if (candidate.size() < query.size()) {
    return std::nullopt;
  }

  size_t n = query.size();
  size_t m = candidate.size();

  std::vector<int> prev_match_at(m, kNegInf);
  std::vector<int> prev_best_upto(m, kNegInf);
  std::vector<int> match_at(m, kNegInf);
  std::vector<int> best_upto(m, kNegInf);

  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < m; ++j) {
      match_at[j] = kNegInf;
      if (lower(candidate[j]) == lower(query[i])) {
        int score_here = kMatchBase + boundary_bonus(candidate, j);
        if (i == 0) {
          match_at[j] = score_here;
        } else if (j > 0) {
          int not_consecutive = prev_best_upto[j - 1];
          int consecutive = prev_match_at[j - 1] == kNegInf
                                 ? kNegInf
                                 : prev_match_at[j - 1] + kConsecutiveBonus;
          int best_predecessor = std::max(not_consecutive, consecutive);
          if (best_predecessor > kNegInf) {
            match_at[j] = score_here + best_predecessor;
          }
        }
      }
      best_upto[j] = (j == 0) ? match_at[j] : std::max(best_upto[j - 1], match_at[j]);
    }
    prev_match_at = match_at;
    prev_best_upto = best_upto;
  }

  int final_score = prev_best_upto[m - 1];
  if (final_score <= kNegInf) {
    return std::nullopt;
  }
  return final_score;
}

std::vector<std::string> fuzzy_filter(std::string_view query,
                                       const std::vector<std::string>& candidates,
                                       size_t max_results) {
  std::vector<std::pair<int, const std::string*>> scored;
  scored.reserve(candidates.size());
  for (const std::string& candidate : candidates) {
    if (std::optional<int> score = fuzzy_score(query, candidate)) {
      scored.emplace_back(*score, &candidate);
    }
  }
  std::stable_sort(scored.begin(), scored.end(),
                    [](const auto& a, const auto& b) { return a.first > b.first; });
  if (scored.size() > max_results) {
    scored.resize(max_results);
  }
  std::vector<std::string> result;
  result.reserve(scored.size());
  for (const auto& [score, candidate] : scored) {
    result.push_back(*candidate);
  }
  return result;
}

}  // namespace zedit::core
