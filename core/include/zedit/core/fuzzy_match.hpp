#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zedit::core {

// Subsequence fuzzy match, like fzf/telescope's "quick open": every
// character of `query` must appear in `candidate` in order
// (case-insensitive), but not necessarily contiguously. Returns a score
// (higher is a better match) if it matches, or nullopt if some query
// character never occurs. An empty query matches everything with a
// neutral score of 0, so an unfiltered list is returned in its original
// order (fuzzy_filter uses a stable sort).
//
// Scoring favors tight, front-loaded matches the way real fuzzy finders
// do: consecutive matched characters score higher than scattered ones,
// and matching right at the start of the string or right after a path
// separator ('/', '_', '-', '.') scores higher still, since that's
// usually where a human's mental "abbreviation" of a filename starts.
std::optional<int> fuzzy_score(std::string_view query, std::string_view candidate);

// Filters `candidates` to those that fuzzy-match `query`, sorted best
// match first (ties keep their original relative order). Caps the result
// at `max_results` -- callers rendering the list in a GUI don't need
// more than a screenful or two, and this keeps a huge candidate set from
// making every keystroke re-sort the whole thing for no visible benefit.
std::vector<std::string> fuzzy_filter(std::string_view query,
                                       const std::vector<std::string>& candidates,
                                       size_t max_results = 200);

}  // namespace zedit::core
