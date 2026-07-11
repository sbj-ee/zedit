#pragma once

#include <cstddef>

#include "zedit/core/editor.hpp"
#include "zedit/core/mode.hpp"
#include "zedit/core/motion.hpp"

namespace zedit::core {

// Executes a charwise operator over the half-open range [range.start,
// range.end). Updates `register_name` (0 = unnamed, 'a'-'z' = named) and
// (for Delete/Change) the buffer, but never touches mode -- callers are
// responsible for entering Insert mode after a Change, and for re-clamping
// the cursor after a Delete (Change intentionally leaves the cursor exactly
// at the deletion point, which may be one past the last remaining
// character).
void execute_operator_charwise(OperatorKind op, Range range, Editor& ed,
                                char register_name = 0);

// Executes a linewise operator (dd/yy/cc) over `count` lines starting at
// start_line.
void execute_operator_linewise(OperatorKind op, size_t start_line, int count,
                                Editor& ed, char register_name = 0);

// Deletes up to `count` characters starting at the cursor, without
// crossing the end of the line. Used by 'x'.
void execute_delete_char(int count, Editor& ed, char register_name = 0);

// Pastes `register_name`'s content (0 = unnamed) before (P) or after (p)
// the cursor, repeated `count` times as a single block.
void execute_paste(bool before, int count, Editor& ed, char register_name = 0);

}  // namespace zedit::core
