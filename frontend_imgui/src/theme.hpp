#pragma once

#include <imgui.h>

#include "zedit/core/highlight.hpp"

namespace zedit::frontend {

ImU32 color_for_token(zedit::core::TokenKind kind);
ImU32 default_text_color();

}  // namespace zedit::frontend
