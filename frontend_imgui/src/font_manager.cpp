#include "font_manager.hpp"

#include <imgui.h>

#include <cstdio>

namespace zedit::frontend {

namespace {

constexpr const char* kCandidatePaths[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/ubuntu/UbuntuMono-Regular.ttf",
    "/System/Library/Fonts/Monaco.ttf",
    "/System/Library/Fonts/Menlo.ttc",
    "/Library/Fonts/Menlo.ttc",
};

}  // namespace

ImFont* load_monospace_font(ImGuiIO& io, float size_px) {
  ImFont* font = nullptr;
  for (const char* path : kCandidatePaths) {
    font = io.Fonts->AddFontFromFileTTF(path, size_px);
    if (font != nullptr) {
      break;
    }
  }
  if (font == nullptr) {
    std::fprintf(stderr,
                  "zedit: no monospace font found, falling back to the "
                  "default ImGui font\n");
    font = io.Fonts->AddFontDefault();
  }
  io.Fonts->Build();
  return font;
}

}  // namespace zedit::frontend
