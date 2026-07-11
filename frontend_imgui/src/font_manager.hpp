#pragma once

struct ImFont;
struct ImGuiIO;

namespace zedit::frontend {

// Loads a monospace font for code rendering by trying a handful of common
// system font locations (Linux/macOS), falling back to ImGui's built-in
// proportional font if none are found. A bundled font is a reasonable
// future improvement once assets get an install/packaging story.
ImFont* load_monospace_font(ImGuiIO& io, float size_px);

}  // namespace zedit::frontend
