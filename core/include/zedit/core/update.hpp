#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zedit::core {

// In-app update model: version comparison, the GitHub release JSON, asset
// selection, SHA256SUMS verification and the "when to check / whom to tell"
// rules. No networking and no UI here, so all of it is unit-tested with
// fixtures. The download/verify/install job built on it is update_service.hpp.
// (Same design as zterminal's updater, written for zedit's non-Qt core.)

// Semantic version (https://semver.org): MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD],
// a leading 'v' allowed (git tags). Missing parts count as 0 ("1.2" = 1.2.0).
struct SemVer {
  int major = 0;
  int minor = 0;
  int patch = 0;
  std::vector<std::string> prerelease;  // "rc.1" -> {"rc", "1"}; empty = a release
  bool valid = false;

  static SemVer parse(std::string_view text);
  std::string to_string() const;
  // <0, 0, >0. Build metadata is ignored; 1.0.0-rc.1 < 1.0.0; numeric
  // prerelease parts compare numerically and sort before alphanumeric ones.
  static int compare(const SemVer& a, const SemVer& b);
};

// True when `candidate` is a newer valid version than `current`.
bool is_newer_version(std::string_view candidate, std::string_view current);

struct ReleaseAsset {
  std::string name;
  std::string url;  // browser_download_url
  int64_t size = 0;
  bool valid() const { return !name.empty() && !url.empty(); }
};

// GET /repos/sbj-ee/zedit/releases/latest, the parts we use.
struct ReleaseInfo {
  std::string tag;  // "v1.8.0"
  SemVer version;   // parsed tag
  std::string name;
  std::string notes;     // body (Markdown)
  std::string html_url;  // release page
  bool draft = false;
  bool prerelease = false;
  std::vector<ReleaseAsset> assets;

  // nullopt (with *error) for JSON that isn't a usable release.
  static std::optional<ReleaseInfo> from_json(std::string_view json, std::string* error = nullptr);
  // zedit-<version>-Linux-amd64.deb (the CPack name for this release's version).
  std::optional<ReleaseAsset> deb_asset() const;
  // zedit-<version>-Darwin.dmg (Apple Silicon DragNDrop package).
  std::optional<ReleaseAsset> dmg_asset() const;
  // This platform's package: the .deb on Linux, the .dmg on macOS.
  std::optional<ReleaseAsset> package_asset() const;
  std::optional<ReleaseAsset> checksum_asset() const;   // "SHA256SUMS"
  std::optional<ReleaseAsset> signature_asset() const;  // "SHA256SUMS.minisig"
};

// The package file name this platform expects for `version` (for messages).
std::string expected_package_name(const SemVer& version);

// SHA256SUMS ("<64 hex>  <name>" or "<64 hex> *<name>" per line, as written by sha256sum).
struct ChecksumResult {
  enum class Status { Ok, Mismatch, NotListed, BadFile, Unreadable };
  Status status = Status::Unreadable;
  std::string expected;  // lower-case hex from SHA256SUMS
  std::string actual;    // lower-case hex of the file
  std::string message() const;
  bool ok() const { return status == Status::Ok; }
};
// Expected hash for file_name, or empty.
std::string checksum_for(std::string_view sha256sums, std::string_view file_name);
ChecksumResult verify_checksum(const std::string& file_path, std::string_view sha256sums,
                               std::string_view file_name);

// The release page we may open in a browser: https://github.com/sbj-ee/zedit/releases/...
// (the URL comes from the API response, so don't open anything else).
bool is_trusted_release_page_url(std::string_view url);

// Updates are only installed when SHA256SUMS carries a valid minisign
// signature (SHA256SUMS.minisig) by the release key: the base64 public key
// line, compiled in (core/src/update_signing_key.cpp). Empty: this build can't
// verify updates, and Install refuses.
std::string update_signing_public_key();
void set_update_signing_public_key_for_tests(std::string base64);  // empty: built-in key again

// Persistent state: $XDG_CONFIG_HOME/zedit/update_state (else ~/.config/zedit/),
// plain "key=value" lines.
struct UpdateState {
  std::optional<int64_t> last_check;  // last automatic check, Unix seconds
  std::string skipped_version;        // "Skip This Version": the tag, e.g. "v1.8.0"
  bool check_at_startup = true;       // Help > Check for Updates at Startup

  static std::string default_path();
  static UpdateState load(const std::string& path);
  bool save(const std::string& path) const;
};

struct UpdatePolicy {
  static constexpr int64_t kMinSecondsBetweenChecks = 24 * 60 * 60;  // at most once a day
  // Automatic check at startup: switched on and the last one is a day old.
  static bool should_check_at_startup(const UpdateState& state, int64_t now);
  // Whether to show the dialog for `release`: it must be newer than the
  // running version; a skipped tag only stays quiet for automatic checks
  // (anything newer than the skipped one is offered again).
  static bool should_offer(const ReleaseInfo& release, std::string_view current_version,
                           const UpdateState& state, bool manual);
};

}  // namespace zedit::core
