#include "app.hpp"

#include <imgui.h>

#include <optional>
#include <string>
#include <utility>

#include "zedit/core/clipboard_text.hpp"

#include "input_map.hpp"
#ifdef __APPLE__
#include "macos_pasteboard.hpp"
#elif defined(__linux__)
#include "linux_clipboard.hpp"
#endif
#include "menu_bar.hpp"
#include "recent_files.hpp"
#include "status_line.hpp"
#include "update_checker.hpp"

namespace zedit::frontend {

App::App(zedit::core::Editor editor, ImFont* font, ImTextureID icon_texture)
    : editor_(std::move(editor)), font_(font), icon_texture_(icon_texture) {
  // Writes go through ImGui's GLFW backend (glfwSetClipboardString:
  // Wayland data-device, X11 CLIPBOARD, or NSPasteboard plain text).
  //
  // Reads always produce plain text. glfwGetClipboardString() only ever
  // asks for plain text, so a copy offering just HTML (some
  // Electron/Chromium paths) or just RTF (some Mac apps) made paste
  // silently do nothing. Instead, on macOS reads go straight to
  // NSPasteboard (read_macos_clipboard_text()); on Linux they go through
  // read_linux_clipboard_text() -- zedit's own X11 CLIPBOARD reader, or
  // wl-paste on Wayland -- which converts those flavors to plain text by
  // the same rule. Elsewhere GLFW's plain-text read goes through the same
  // normalization.
#if defined(__linux__) && !defined(__APPLE__)
  // Every clipboard write zedit makes -- the editor's yanks through the
  // bridge below and Ctrl-C in ImGui text fields alike -- goes through
  // ImGui's GLFW setter. Wrapped (not replaced) so the X11 reader can tell
  // when zedit itself owns the clipboard; see note_linux_clipboard_write().
  static void (*glfw_set_clipboard)(ImGuiContext*, const char*) = nullptr;
  ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
  if (platform_io.Platform_SetClipboardTextFn != nullptr && glfw_set_clipboard == nullptr) {
    glfw_set_clipboard = platform_io.Platform_SetClipboardTextFn;
    platform_io.Platform_SetClipboardTextFn = [](ImGuiContext* ctx, const char* text) {
      glfw_set_clipboard(ctx, text);
      note_linux_clipboard_write();
    };
  }
#endif
  editor_.set_clipboard_bridge(zedit::core::Editor::ClipboardBridge{
      []() -> std::optional<std::string> {
#ifdef __APPLE__
        return read_macos_clipboard_text();
#elif defined(__linux__)
        return read_linux_clipboard_text();
#else
        const char* text = ImGui::GetClipboardText();
        if (text == nullptr) return std::nullopt;
        zedit::core::ClipboardFlavors flavors;
        flavors.plain_text = std::string(text);
        return zedit::core::clipboard_plain_text(flavors);
#endif
      },
      [](const std::string& text) { ImGui::SetClipboardText(text.c_str()); },
  });
}

void App::render_frame(ImGuiIO& io, UpdateChecker& update_checker) {
  editor_.poll_lsp();
  editor_.poll_recovery();

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

  // While any modal popup is open, no keystroke should also reach the
  // editor underneath -- otherwise typing into a popup's field (or even
  // just pressing keys over a popup with no text field at all, like
  // About) simultaneously fires vim keybindings against the document.
  // Gated on "any popup open" rather than io.WantTextInput specifically:
  // the latter is only true while some *text widget* has focus, which
  // depends on every popup remembering to call SetKeyboardFocusHere on
  // one (missed twice already -- see file_dialog.cpp and
  // find_replace_dialog.cpp's history) and is simply false for a
  // text-free popup like About, where input would otherwise leak through
  // for as long as it's open.
  bool any_popup_open =
      ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
  if (!any_popup_open) {
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

  render_menu_bar(editor_, icon_texture_, word_wrap_, show_whitespace_, update_checker,
                   available_update_);

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
    bool clicked = text_views_[i].render(editor_, font_, pane_height, pane_width, word_wrap_,
                                          show_whitespace_);
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

  // Crash-recovery offer: deferred to the first frame after open so ImGui is
  // up (CLI initial path included). OpenPopup once when the offer appears.
  if (editor_.pending_recovery().has_value()) {
    if (!recovery_popup_opened_) {
      ImGui::OpenPopup("Recover unsaved changes");
      recovery_popup_opened_ = true;
    }
  } else {
    recovery_popup_opened_ = false;
  }
  if (ImGui::BeginPopupModal("Recover unsaved changes", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    const auto& offer = editor_.pending_recovery();
    std::string label = offer ? offer->path : std::string{};
    // Show basename when possible.
    auto slash = label.find_last_of("/\\");
    if (slash != std::string::npos) {
      label = label.substr(slash + 1);
    }
    ImGui::Text("Recover unsaved changes to \"%s\"?", label.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Recover", ImVec2(120, 0))) {
      editor_.accept_recovery();
      ImGui::CloseCurrentPopup();
      recovery_popup_opened_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(120, 0))) {
      editor_.discard_recovery();
      ImGui::CloseCurrentPopup();
      recovery_popup_opened_ = false;
    }
    ImGui::EndPopup();
  }


}

}  // namespace zedit::frontend
