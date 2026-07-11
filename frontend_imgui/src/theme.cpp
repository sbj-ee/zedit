#include "theme.hpp"

#include <string_view>

namespace zedit::frontend {

using zedit::core::DiagnosticSeverity;
using zedit::core::RgbColor;
using zedit::core::TokenKind;

namespace {

std::unordered_map<std::string, ImU32> g_overrides;

std::string_view name_for_token(TokenKind kind) {
  switch (kind) {
    case TokenKind::Comment:
      return "comment";
    case TokenKind::String:
      return "string";
    case TokenKind::Number:
      return "number";
    case TokenKind::Keyword:
      return "keyword";
    case TokenKind::Type:
      return "type";
    case TokenKind::Function:
      return "function";
    case TokenKind::Variable:
      return "variable";
    case TokenKind::Operator:
    case TokenKind::Punctuation:
      return "text";
    case TokenKind::Constant:
      return "constant";
    case TokenKind::None:
      return "text";
  }
  return "text";
}

std::string_view name_for_severity(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::Error:
      return "error";
    case DiagnosticSeverity::Warning:
      return "warning";
    case DiagnosticSeverity::Information:
      return "information";
    case DiagnosticSeverity::Hint:
      return "hint";
  }
  return "error";
}

ImU32 default_for_name(std::string_view name) {
  if (name == "comment") return IM_COL32(106, 153, 85, 255);
  if (name == "string") return IM_COL32(206, 145, 120, 255);
  if (name == "number") return IM_COL32(181, 206, 168, 255);
  if (name == "keyword") return IM_COL32(197, 134, 192, 255);
  if (name == "type") return IM_COL32(78, 201, 176, 255);
  if (name == "function") return IM_COL32(220, 220, 170, 255);
  if (name == "variable") return IM_COL32(156, 220, 254, 255);
  if (name == "constant") return IM_COL32(100, 180, 255, 255);
  if (name == "error") return IM_COL32(240, 80, 80, 255);
  if (name == "warning") return IM_COL32(220, 180, 60, 255);
  if (name == "information") return IM_COL32(90, 170, 230, 255);
  if (name == "hint") return IM_COL32(150, 150, 150, 255);
  return IM_COL32(220, 220, 220, 255);  // "text" and anything unrecognized
}

ImU32 color_for_name(std::string_view name) {
  auto it = g_overrides.find(std::string(name));
  return it != g_overrides.end() ? it->second : default_for_name(name);
}

}  // namespace

ImU32 default_text_color() { return color_for_name("text"); }

ImU32 color_for_token(TokenKind kind) { return color_for_name(name_for_token(kind)); }

ImU32 color_for_severity(DiagnosticSeverity severity) {
  return color_for_name(name_for_severity(severity));
}

void apply_color_overrides(const std::unordered_map<std::string, RgbColor>& colors) {
  for (const auto& [name, rgb] : colors) {
    g_overrides[name] = IM_COL32(rgb.r, rgb.g, rgb.b, 255);
  }
}

}  // namespace zedit::frontend
