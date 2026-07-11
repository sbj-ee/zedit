#pragma once

#include <imgui.h>

#include "zedit/core/highlight.hpp"
#include "zedit/core/lsp.hpp"

namespace zedit::frontend {

ImU32 color_for_token(zedit::core::TokenKind kind);
ImU32 default_text_color();
ImU32 color_for_severity(zedit::core::DiagnosticSeverity severity);

}  // namespace zedit::frontend
