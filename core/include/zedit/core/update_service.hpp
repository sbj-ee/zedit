#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "zedit/core/update.hpp"

namespace zedit::core {

// Everything the updater reads from the outside world, so the tests can point
// it at a local server and a harmless "installer". Defaults are production.
struct UpdateConfig {
  std::string current_version;  // e.g. version_string()
  std::string api_url = "https://api.github.com/repos/sbj-ee/zedit/releases/latest";
  // Network access goes through curl (already how zedit checks for updates):
  // no HTTP/TLS library is linked into the editor.
  std::string curl = "curl";
  // Production only ever speaks https (also across redirects); the tests'
  // local server is plain http.
  bool https_only = true;
  int connect_timeout_s = 10;
  int check_timeout_s = 15;
  int stall_timeout_s = 30;  // a download that moves < 1 byte/s this long is abandoned

  // Linux: only a packaged install is replaced (see install_package()).
  std::string installed_binary = "/usr/bin/ze";
  std::string pkexec = "/usr/bin/pkexec";
  std::string apt = "/usr/bin/apt";
  // macOS: opens the verified .dmg.
  std::string open_command = "/usr/bin/open";
  // Tests: run this program (the staged package's absolute path is appended)
  // instead of `pkexec apt install -y`; also skips the installed-binary check.
  std::vector<std::string> installer_override;
  // Where the download and staging folders go; empty = $TMPDIR, else /tmp.
  std::string temp_root;
};

enum class UpdateStage { Idle, Checking, Downloading, Verifying, Installing };

struct UpdateCheckResult {
  enum class Outcome { Release, NoRelease, Error };
  Outcome outcome = Outcome::Error;
  ReleaseInfo release;  // when outcome == Release
  std::string error;    // human-readable, for NoRelease / Error
  bool rate_limited = false;
  bool manual = false;  // the check was asked for (Help menu), not the quiet startup one
};

struct UpdateInstallResult {
  enum class Outcome {
    Installed,       // Linux: apt installed the package
    DiskImageOpened,  // macOS: the verified .dmg is open for drag-to-Applications
    Failed,          // nothing installed; message says why
    Cancelled,       // the user cancelled the download
  };
  Outcome outcome = Outcome::Failed;
  std::string message;
  // After a Failed install of a package that did verify: where it is, for a
  // manual install. Empty otherwise.
  std::string kept_package;
  std::string tag;  // the release this was about
};

struct UpdateProgress {
  std::string asset;  // file being downloaded
  int64_t received = 0;
  int64_t total = -1;  // -1: unknown
};

// One background job at a time: a release check, or download -> verify ->
// install. Results are picked up from the main thread with poll_*() (once per
// frame). Never moved: it is a main()-local (see frontend update_ui.hpp).
//
// Install policy (the same as zterminal's): the package is installed only when
// SHA256SUMS carries a valid minisign signature by the release key
// (update_signing_public_key()), checked *before* the package is even
// downloaded, and the package matches the signed hash. Anything missing,
// invalid or mismatched is refused with a message. The verified package is
// copied to a private (0700) folder and re-hashed right before the installer
// gets it.
class UpdateService {
 public:
  explicit UpdateService(UpdateConfig config);
  ~UpdateService();
  UpdateService(const UpdateService&) = delete;
  UpdateService& operator=(const UpdateService&) = delete;

  // No-ops while another job runs.
  void start_check(bool manual);
  void start_install(ReleaseInfo release);
  // Cancels a running download (not the installer: apt is never interrupted).
  void cancel();

  UpdateStage stage() const;
  bool busy() const { return stage() != UpdateStage::Idle; }
  UpdateProgress progress() const;

  // Each returns the result once, after the job finished.
  std::optional<UpdateCheckResult> poll_check();
  std::optional<UpdateInstallResult> poll_install();

  const UpdateConfig& config() const;

  // The install command for a staged package: {pkexec, apt, "install", "-y",
  // <absolute path>}. Absolute paths only. Exposed for the tests.
  static std::vector<std::string> install_command(const UpdateConfig& config,
                                                  const std::string& package_path);

 // Implementation detail (shared with the worker thread); public only so the
  // .cpp's helpers can name it.
  struct Shared;

 private:
  std::shared_ptr<Shared> shared_;
  std::thread thread_;
};

}  // namespace zedit::core
