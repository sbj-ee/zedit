#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "zedit/core/editor.hpp"
#include "zedit/core/update.hpp"
#include "zedit/core/update_service.hpp"

namespace zedit::frontend {

// The in-app updater's UI and state machine: checks GitHub for a newer
// release, offers it (Install / Later / Skip This Version), shows download
// progress, then offers a restart. The work happens in zedit::core::
// UpdateService (background thread); this class just drives it from the
// render loop and draws the modals.
//
// Like the old UpdateChecker it replaces, this is a plain main()-local that is
// never moved: Editor and App get move-constructed during startup, and the
// service's thread must not outlive an owner whose address changes.
class UpdateUi {
 public:
  UpdateUi(zedit::core::UpdateConfig config, std::string state_path);

  // The quiet check at startup: only when switched on and the last automatic
  // check is more than a day old. Never shows an error.
  void start_startup_check();
  // Help > Check for Updates: always checks, and always says what happened.
  void check_now();

  // Once per frame, before drawing: collects finished work.
  void poll();
  // Help menu entries (inside BeginMenu("Help")).
  void render_help_menu_items();
  // The modals. Call once per frame at the top level of the ImGui frame
  // (after the menu bar). `ed` is only read (for unsaved-changes warnings).
  void render_modals(const zedit::core::Editor& ed);

  // The Install button's action (also what the tests press).
  void accept_offer() { begin_install(); }
  enum class Phase { None, Offer, Progress, Message, Restart };
  Phase phase() const { return phase_; }

  // After the install: the user chose "Restart now". main() re-executes the
  // installed binary once the window is closed.
  bool restart_requested() const { return restart_requested_; }
  const std::string& restart_binary() const { return service_.config().installed_binary; }

 private:
  void show_message(std::string title, std::string text);
  void begin_install();
  void set_check_at_startup(bool on);
  void save_state() const;

  zedit::core::UpdateService service_;
  std::string state_path_;
  zedit::core::UpdateState state_;

  Phase phase_ = Phase::None;
  bool open_popup_ = false;  // OpenPopup on the next render_modals()
  std::optional<zedit::core::ReleaseInfo> offered_;  // the release in the Offer/Progress phases
  std::optional<zedit::core::ReleaseInfo> pending_;  // a quiet offer waiting for a free moment
  std::string message_title_;
  std::string message_text_;
  std::string message_release_url_;  // "Release Page" button in a message, when set
  std::string installed_tag_;
  bool restart_requested_ = false;
};

}  // namespace zedit::frontend
