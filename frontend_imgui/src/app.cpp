#include "app.hpp"

#include <imgui.h>

#include <utility>

#include "input_map.hpp"
#include "status_line.hpp"
#include "tab_bar.hpp"

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

  render_tab_bar(editor_);

  float status_line_height = ImGui::GetTextLineHeightWithSpacing() + 6.0f;
  float content_height = ImGui::GetContentRegionAvail().y - status_line_height;
  float content_width = ImGui::GetContentRegionAvail().x;

  size_t window_count = editor_.window_count();
  if (text_views_.size() != window_count) {
    text_views_.resize(window_count);
  }

  size_t real_current = editor_.current_window_index();
  size_t focus_after_click = real_current;
  bool side_by_side = editor_.split_layout() == zedit::core::SplitLayout::SideBySide;

  float pane_height =
      side_by_side ? content_height : content_height / static_cast<float>(window_count);
  float pane_width = side_by_side ? content_width / static_cast<float>(window_count) : 0.0f;

  for (size_t i = 0; i < window_count; ++i) {
    editor_.set_current_window(i);
    bool focused = (i == real_current);

    ImGui::PushID(static_cast<int>(i));
    if (focused && window_count > 1) {
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.40f, 0.55f, 0.90f, 1.0f));
    }
    bool clicked = text_views_[i].render(editor_, font_, pane_height, pane_width);
    if (focused && window_count > 1) {
      ImGui::PopStyleColor();
    }
    ImGui::PopID();

    if (clicked) {
      focus_after_click = i;
    }
    if (side_by_side && i + 1 < window_count) {
      ImGui::SameLine();
    }
  }
  editor_.set_current_window(focus_after_click);

  render_status_line(editor_);

  ImGui::End();
}

}  // namespace zedit::frontend
