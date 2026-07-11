#pragma once

#include <cstddef>

#include "zedit/core/piece_table.hpp"

namespace zedit::core {

// A half-open byte range [start, end) that an operator acts on.
struct Range {
  size_t start;
  size_t end;
};

// Motions return the target offset a cursor would land on. Callers combine
// a motion's target with the original cursor offset to build an operator
// Range: exclusive motions (w, 0) exclude whichever endpoint is reached by
// moving forward; inclusive motions (e, $) include it. See mode_state_machine.cpp
// for how ranges are assembled from these.
size_t motion_word_forward(const PieceTable& buf, size_t offset);      // w
size_t motion_word_backward(const PieceTable& buf, size_t offset);     // b
size_t motion_word_end_forward(const PieceTable& buf, size_t offset);  // e
size_t motion_line_start(const PieceTable& buf, size_t offset);        // 0
size_t motion_line_end(const PieceTable& buf, size_t offset);          // $ (inclusive)

// "inner word": the contiguous run of the same character class (word,
// punctuation, or whitespace) containing offset. Used by text objects like
// "iw" (e.g. ciw, diw, yiw).
Range text_object_inner_word(const PieceTable& buf, size_t offset);

}  // namespace zedit::core
