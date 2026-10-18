#include "update_ui.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "open_url.hpp"
#include "zedit/core/version.hpp"

namespace zedit::frontend {

namespace {

int64_t now_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

constexpr const char* kPopupName = "Update";

std::string human_bytes(int64_t bytes) {
  char buf[32];
  if (bytes >= 1024 * 1024) {
    std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else {
    std::snprintf(buf, sizeof buf, "%.0f KB", static_cast<double>(bytes) / 1024.0);
  }
  return buf;
}

}  // namespace

UpdateUi::UpdateUi(zedit::core::UpdateConfig config, std::string state_path)
    : service_(std::move(config)),
      state_path_(std::move(state_path)),
      state_(zedit::core::UpdateState::load(state_path_)) {}

void UpdateUi::save_state() const { state_.save(state_path_); }

void UpdateUi::set_check_at_startup(bool on) {
  state_.check_at_startup = on;
  save_state();
}

void UpdateUi::start_startup_check() {
  if (!zedit::core::UpdatePolicy::should_check_at_startup(state_, now_seconds())) return;
  state_.last_check = now_seconds();
  save_state();
  service_.start_check(/*manual=*/false);
}

void UpdateUi::check_now() {
  if (service_.busy() || phase_ != Phase::None) return;
  service_.start_check(/*manual=*/true);
}

void UpdateUi::show_message(std::string title, std::string text) {
  message_title_ = std::move(title);
  message_text_ = std::move(text);
  phase_ = Phase::Message;
  open_popup_ = true;
}

void UpdateUi::begin_install() {
  if (!offered_) return;
  service_.start_install(*offered_);
  phase_ = Phase::Progress;
}

void UpdateUi::poll() {
  if (auto check = service_.poll_check()) {
    using Outcome = zedit::core::UpdateCheckResult::Outcome;
    if (check->outcome == Outcome::Release) {
      const std::string current = zedit::core::version_string();
      if (zedit::core::UpdatePolicy::should_offer(check->release, current, state_, check->manual)) {
        if (check->manual) {
          offered_ = check->release;
          phase_ = Phase::Offer;
          open_popup_ = true;
        } else {
          pending_ = check->release;  // shown when nothing else is open
        }
      } else if (check->manual) {
        show_message("Check for Updates", "zedit " + current + " is up to date.");
      }
    } else if (check->manual) {
      show_message("Check for Updates",
                   check->outcome == Outcome::NoRelease
                       ? "No published zedit release was found."
                       : check->error);
    }
  }
  if (auto install = service_.poll_install()) {
    using Outcome = zedit::core::UpdateInstallResult::Outcome;
    installed_tag_ = install->tag;
    switch (install->outcome) {
      case Outcome::Installed:
#if defined(__APPLE__)
        show_message("Update installed", install->message);
#else
        phase_ = Phase::Restart;
        open_popup_ = true;
#endif
        break;
      case Outcome::DiskImageOpened:
        show_message("Update downloaded", install->message);
        break;
      case Outcome::Cancelled:
        phase_ = Phase::None;
        break;
      case Outcome::Failed:
        show_message("Update failed", install->message);
        break;
    }
    if (install->outcome == Outcome::Failed && offered_ &&
        zedit::core::is_trusted_release_page_url(offered_->html_url)) {
      message_release_url_ = offered_->html_url;
    } else {
      message_release_url_.clear();
    }
  }
}

void UpdateUi::render_help_menu_items() {
  const bool busy = service_.busy() || phase_ != Phase::None;
  const bool checking = service_.stage() == zedit::core::UpdateStage::Checking;
  if (ImGui::MenuItem(checking ? "Checking for Updates..." : "Check for Updates", nullptr, false,
                      !busy)) {
    check_now();
  }
  bool at_startup = state_.check_at_startup;
  if (ImGui::MenuItem("Check for Updates at Startup", nullptr, &at_startup)) {
    set_check_at_startup(at_startup);
  }
  if (pending_ && phase_ == Phase::None) {
    const std::string label = "Update " + pending_->tag + " available...";
    if (ImGui::MenuItem(label.c_str())) {
      offered_ = pending_;
      pending_.reset();
      phase_ = Phase::Offer;
      open_popup_ = true;
    }
  }
}

void UpdateUi::render_modals(const zedit::core::Editor& ed) {
  // A quiet offer from the startup check waits until nothing else is open
  // (it must not steal the focus from a dialog the user is typing in).
  if (pending_ && phase_ == Phase::None &&
      !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
    offered_ = pending_;
    pending_.reset();
    phase_ = Phase::Offer;
    open_popup_ = true;
  }
  if (open_popup_) {
    ImGui::OpenPopup(kPopupName);
    open_popup_ = false;
  }
  if (phase_ == Phase::None && !ImGui::IsPopupOpen(kPopupName)) return;
  if (!ImGui::BeginPopupModal(kPopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  if (phase_ == Phase::None) {  // finished off-screen (e.g. a cancelled download)
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
  }
  const float wrap = ImGui::GetFontSize() * 34.0f;
  ImGui::PushTextWrapPos(wrap);

  switch (phase_) {
    case Phase::Offer: {
      const zedit::core::ReleaseInfo& r = *offered_;
      const std::string current = zedit::core::version_string();
      ImGui::Text("zedit %s is available (you have %s).", r.tag.c_str(), current.c_str());
      if (!r.notes.empty()) {
        ImGui::Spacing();
        std::string notes = r.notes.size() > 2000 ? r.notes.substr(0, 2000) + "\n..." : r.notes;
        ImGui::BeginChild("update_notes", ImVec2(wrap, ImGui::GetTextLineHeightWithSpacing() * 9),
                          ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(notes.c_str());
        ImGui::EndChild();
      }
      const bool signed_release = r.package_asset() && r.checksum_asset() && r.signature_asset();
      ImGui::Spacing();
      if (!signed_release) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f),
                           "This release isn't signed (it has no SHA256SUMS.minisig, or no package "
                           "for this system), so zedit won't install it. Download it from the "
                           "release page yourself.");
      } else {
#if defined(__APPLE__)
        ImGui::TextUnformatted("The download is verified, then the disk image opens so you can "
                               "drag zedit to Applications.");
#else
        ImGui::TextUnformatted("The download is verified (signature and SHA-256) before anything "
                               "is installed. You'll be asked for your password by the system.");
#endif
      }
      ImGui::Separator();
      if (signed_release) {
        if (ImGui::Button("Install")) begin_install();
        ImGui::SameLine();
      }
      if (zedit::core::is_trusted_release_page_url(r.html_url)) {
        if (ImGui::Button("Release Page")) open_url(r.html_url);
        ImGui::SameLine();
      }
      if (ImGui::Button("Skip This Version")) {
        state_.skipped_version = r.tag;
        save_state();
        phase_ = Phase::None;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Later") || (phase_ == Phase::Offer && ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        phase_ = Phase::None;
        ImGui::CloseCurrentPopup();
      }
      break;
    }
    case Phase::Progress: {
      const zedit::core::UpdateStage stage = service_.stage();
      const zedit::core::UpdateProgress p = service_.progress();
      const char* what = "Working...";
      switch (stage) {
        case zedit::core::UpdateStage::Downloading: what = "Downloading"; break;
        case zedit::core::UpdateStage::Verifying: what = "Verifying"; break;
        case zedit::core::UpdateStage::Installing: what = "Installing"; break;
        default: break;
      }
      if (stage == zedit::core::UpdateStage::Downloading && !p.asset.empty()) {
        ImGui::Text("%s %s", what, p.asset.c_str());
        float fraction = p.total > 0 ? static_cast<float>(p.received) / static_cast<float>(p.total) : 0.0f;
        fraction = std::clamp(fraction, 0.0f, 1.0f);
        std::string overlay = human_bytes(p.received);
        if (p.total > 0) overlay += " / " + human_bytes(p.total);
        ImGui::ProgressBar(p.total > 0 ? fraction : -1.0f * static_cast<float>(ImGui::GetTime()),
                           ImVec2(wrap, 0.0f), overlay.c_str());
      } else if (stage == zedit::core::UpdateStage::Installing) {
        ImGui::TextUnformatted("Installing... (enter your password in the system prompt)");
      } else if (stage != zedit::core::UpdateStage::Idle) {
        ImGui::Text("%s...", what);
      } else {
        ImGui::TextUnformatted("Finishing...");
      }
      ImGui::Separator();
      const bool can_cancel = stage == zedit::core::UpdateStage::Downloading ||
                              stage == zedit::core::UpdateStage::Verifying;
      ImGui::BeginDisabled(!can_cancel);
      if (ImGui::Button("Cancel")) service_.cancel();
      ImGui::EndDisabled();
      // The result arrives through poll(), which moves the phase on.
      if (phase_ != Phase::Progress) ImGui::CloseCurrentPopup();
      break;
    }
    case Phase::Message: {
      ImGui::TextUnformatted(message_title_.c_str());
      ImGui::Separator();
      // Selectable text: a failed install shows the manual command to copy.
      ImGui::InputTextMultiline("##update_message", message_text_.data(), message_text_.size() + 1,
                                ImVec2(wrap, ImGui::GetTextLineHeightWithSpacing() * 8),
                                ImGuiInputTextFlags_ReadOnly);
      ImGui::Separator();
      if (!message_release_url_.empty()) {
        if (ImGui::Button("Release Page")) open_url(message_release_url_);
        ImGui::SameLine();
      }
      if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        phase_ = Phase::None;
        message_release_url_.clear();
        ImGui::CloseCurrentPopup();
      }
      break;
    }
    case Phase::Restart: {
      size_t dirty = 0;
      for (size_t i = 0; i < ed.buffer_count(); ++i) {
        if (ed.buffer_dirty(i)) ++dirty;
      }
      ImGui::Text("zedit %s was installed.", installed_tag_.c_str());
      ImGui::Spacing();
      if (dirty > 0) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f),
                           "%zu buffer(s) have unsaved changes. Save them, then restart.", dirty);
      } else {
        ImGui::TextUnformatted("Restart zedit now to use the new version? The current file "
                               "reopens.");
      }
      ImGui::Separator();
      ImGui::BeginDisabled(dirty > 0);
      if (ImGui::Button("Restart Now")) {
        restart_requested_ = true;
        phase_ = Phase::None;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Later") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        phase_ = Phase::None;
        ImGui::CloseCurrentPopup();
      }
      break;
    }
    case Phase::None:
      break;
  }
  ImGui::PopTextWrapPos();
  ImGui::EndPopup();
}

}  // namespace zedit::frontend
