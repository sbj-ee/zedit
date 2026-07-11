#pragma once

#include <memory>
#include <string_view>

#include "zedit/core/highlight.hpp"

namespace zedit::core {

// Picks a Highlighter based on filename extension, falling back to
// PlainHighlighter for anything without a grammar yet.
std::unique_ptr<Highlighter> make_highlighter_for_filename(std::string_view filename);

// Whether `filename` looks like a C++ source/header -- used to decide
// whether to talk to clangd, the only language server wired up so far.
bool is_cpp_filename(std::string_view filename);

}  // namespace zedit::core
