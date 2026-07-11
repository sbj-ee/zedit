#pragma once

#include <optional>
#include <string_view>

#include "zedit/core/piece_table.hpp"

namespace zedit::core {

// Plain substring search (no regex) with wraparound. search_forward looks
// strictly after from_offset first, then wraps to the start of the buffer;
// search_backward is the mirror image. Both materialize the document once
// per call, which is fine for a user-triggered action (/, n, N, *, #) but
// would not be for a per-frame hot path.
std::optional<size_t> search_forward(const PieceTable& buf, size_t from_offset,
                                      std::string_view pattern);
std::optional<size_t> search_backward(const PieceTable& buf, size_t from_offset,
                                       std::string_view pattern);

}  // namespace zedit::core
