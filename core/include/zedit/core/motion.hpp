#pragma once

#include <cstddef>
#include <optional>

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

// True for any of ()[]{} -- exposed so the frontend can decide whether to
// draw the matching-bracket highlight without duplicating this list.
bool is_bracket_char(char c);

// "%" -- if `offset` isn't itself on a bracket, first searches forward on
// the current line only for one (matching vim's own "%", which doesn't
// search across lines to *find* a bracket, only to find its *match* once
// one is found). Returns nullopt if there's no bracket on the line, or no
// matching bracket exists (unbalanced).
std::optional<size_t> motion_matching_bracket(const PieceTable& buf, size_t offset);

}  // namespace zedit::core
