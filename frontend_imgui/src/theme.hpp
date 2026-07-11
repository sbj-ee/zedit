#pragma once

#include <imgui.h>

#include <string>
#include <unordered_map>

#include "zedit/core/config.hpp"
#include "zedit/core/highlight.hpp"
#include "zedit/core/lsp.hpp"

namespace zedit::frontend {

ImU32 color_for_token(zedit::core::TokenKind kind);
ImU32 default_text_color();
ImU32 color_for_severity(zedit::core::DiagnosticSeverity severity);

// Applied on top of the built-in defaults, keyed by the same names as the
// zedit.set_color() Lua API: "text", each TokenKind's lowercase name
// (comment/string/number/keyword/type/function/variable/constant), and
// each DiagnosticSeverity's lowercase name (error/warning/information/
// hint). Unrecognized names are stored but never consulted by anything,
// i.e. silently inert -- see the config's lack of name validation for why.
void apply_color_overrides(const std::unordered_map<std::string, zedit::core::RgbColor>& colors);

}  // namespace zedit::frontend
