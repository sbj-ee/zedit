#include "app.hpp"

#include <imgui.h>

#include <utility>

#include "input_map.hpp"
#include "status_line.hpp"

namespace zedit::frontend {

App::App(zedit::core::Editor editor, ImFont* font)
    : editor_(std::move(editor)), font_(font) {}

void App::render_frame(ImGuiIO& io) {
  for (const zedit::core::KeyEvent& ev : collect_key_events(io)) {
    editor_.handle_key(ev);
  }

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                            ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoCollapse |
                            ImGuiWindowFlags_NoBringToFrontOnFocus;
  ImGui::Begin("zedit_main", nullptr, flags);

  float status_line_height = ImGui::GetTextLineHeightWithSpacing() + 6.0f;
  float text_view_height =
      ImGui::GetContentRegionAvail().y - status_line_height;
  text_view_.render(editor_, font_, text_view_height);
  render_status_line(editor_);

  ImGui::End();
}

}  // namespace zedit::frontend
