#include "app.hpp"

#include <imgui.h>

#include <utility>

#include "input_map.hpp"
#include "menu_bar.hpp"
#include "recent_files.hpp"
#include "status_line.hpp"
#include "update_checker.hpp"

namespace zedit::frontend {

App::App(zedit::core::Editor editor, ImFont* font, ImTextureID icon_texture)
    : editor_(std::move(editor)), font_(font), icon_texture_(icon_texture) {}

void App::render_frame(ImGuiIO& io, UpdateChecker& update_checker) {
  editor_.poll_lsp();

  if (!available_update_.has_value()) {
    if (std::optional<zedit::core::UpdateInfo> found = update_checker.poll()) {
      available_update_ = std::move(found);
    }
  }

  // Recorded here rather than at each individual "a file got opened" call
  // site (:e, the Open dialog, Save As, the initial CLI arg) -- catching
  // every filename change in one place, regardless of how it happened.
  if (!editor_.filename().empty() && editor_.filename() != last_recorded_filename_) {
    add_recent_file(editor_.filename());
    last_recorded_filename_ = editor_.filename();
  }

  // While a popup text field (e.g. the menu bar's "Open..."/"Save As..."
  // path box) wants keyboard input, none of it should also reach the
  // editor -- otherwise typing a path would simultaneously type into the
  // document underneath.
  if (!io.WantTextInput) {
    for (const zedit::core::KeyEvent& ev : collect_key_events(io)) {
      // Any keypress dismisses a currently-shown hover popup (it still
      // goes on to perform its normal action -- this isn't a modal "eat
      // the key that closes me" popup, just a tooltip that clears on the
      // next input).
      if (editor_.hover_text().has_value()) {
        editor_.dismiss_hover();
      }
      editor_.handle_key(ev);
    }
  }

  render_menu_bar(editor_, icon_texture_, word_wrap_, update_checker, available_update_);

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
    bool clicked = text_views_[i].render(editor_, font_, pane_height, pane_width, word_wrap_);
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

  const std::optional<std::string>& hover = editor_.hover_text();
  if (hover.has_value() && real_current < text_views_.size()) {
    ImVec2 anchor = text_views_[real_current].cursor_screen_pos();
    ImGui::SetNextWindowPos(ImVec2(anchor.x, anchor.y + ImGui::GetTextLineHeightWithSpacing()));
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGuiWindowFlags hover_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoFocusOnAppearing |
                                    ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("zedit_hover_popup", nullptr, hover_flags);
    ImGui::PushFont(font_);
    ImGui::TextUnformatted(hover->c_str());
    ImGui::PopFont();
    ImGui::End();
  }
}

}  // namespace zedit::frontend
