#include "theme.hpp"

namespace zedit::frontend {

using zedit::core::TokenKind;

ImU32 default_text_color() { return IM_COL32(220, 220, 220, 255); }

ImU32 color_for_token(TokenKind kind) {
  switch (kind) {
    case TokenKind::Comment:
      return IM_COL32(106, 153, 85, 255);
    case TokenKind::String:
      return IM_COL32(206, 145, 120, 255);
    case TokenKind::Number:
      return IM_COL32(181, 206, 168, 255);
    case TokenKind::Keyword:
      return IM_COL32(197, 134, 192, 255);
    case TokenKind::Type:
      return IM_COL32(78, 201, 176, 255);
    case TokenKind::Function:
      return IM_COL32(220, 220, 170, 255);
    case TokenKind::Variable:
      return IM_COL32(156, 220, 254, 255);
    case TokenKind::Operator:
    case TokenKind::Punctuation:
      return default_text_color();
    case TokenKind::Constant:
      return IM_COL32(100, 180, 255, 255);
    case TokenKind::None:
      return default_text_color();
  }
  return default_text_color();
}

}  // namespace zedit::frontend
