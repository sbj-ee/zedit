#include "zedit/core/update.hpp"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "zedit/core/sha256.hpp"

namespace zedit::core {

namespace {

bool is_numeric(std::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::vector<std::string> split(std::string_view s, char sep) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (true) {
    size_t at = s.find(sep, start);
    if (at == std::string_view::npos) {
      parts.emplace_back(s.substr(start));
      break;
    }
    parts.emplace_back(s.substr(start, at - start));
    start = at + 1;
  }
  return parts;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
    s.remove_suffix(1);
  }
  return s;
}

std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

bool is_hex64(std::string_view s) {
  return s.size() == 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
           return std::isxdigit(c) != 0;
         });
}

std::string config_dir() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
    return std::string(xdg) + "/zedit";
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::string(home) + "/.config/zedit";
  }
  return {};
}

}  // namespace

SemVer SemVer::parse(std::string_view text) {
  SemVer v;
  std::string_view s = trim(text);
  if (!s.empty() && (s.front() == 'v' || s.front() == 'V')) {
    s.remove_prefix(1);
  }
  if (size_t plus = s.find('+'); plus != std::string_view::npos) {
    s = s.substr(0, plus);  // build metadata: ignored
  }
  std::string_view pre;
  bool has_pre = false;
  size_t dash = s.find('-');
  if (dash == std::string_view::npos) {
    dash = s.find('~');  // Debian spells prereleases with '~'
  }
  if (dash != std::string_view::npos) {
    has_pre = true;
    pre = s.substr(dash + 1);
    s = s.substr(0, dash);
  }
  std::vector<std::string> parts = split(s, '.');
  if (parts.empty() || parts.size() > 3) {
    return v;
  }
  int nums[3] = {0, 0, 0};
  for (size_t i = 0; i < parts.size(); ++i) {
    if (!is_numeric(parts[i]) || parts[i].size() > 9) {
      return v;
    }
    nums[i] = std::atoi(parts[i].c_str());
  }
  v.major = nums[0];
  v.minor = nums[1];
  v.patch = nums[2];
  if (has_pre) {
    if (pre.empty()) {
      return SemVer{};  // "1.0.0-"
    }
    v.prerelease = split(pre, '.');
    for (const std::string& p : v.prerelease) {
      if (p.empty()) {
        return SemVer{};
      }
    }
  }
  v.valid = true;
  return v;
}

std::string SemVer::to_string() const {
  std::string s = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
  for (size_t i = 0; i < prerelease.size(); ++i) {
    s += (i == 0 ? '-' : '.');
    s += prerelease[i];
  }
  return s;
}

int SemVer::compare(const SemVer& a, const SemVer& b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  // A release ranks above any of its prereleases.
  if (a.prerelease.empty() || b.prerelease.empty()) {
    if (a.prerelease.empty() == b.prerelease.empty()) return 0;
    return a.prerelease.empty() ? 1 : -1;
  }
  const size_t common = std::min(a.prerelease.size(), b.prerelease.size());
  for (size_t i = 0; i < common; ++i) {
    const std::string& x = a.prerelease[i];
    const std::string& y = b.prerelease[i];
    const bool nx = is_numeric(x);
    const bool ny = is_numeric(y);
    if (nx && ny) {
      const long long ix = std::atoll(x.c_str());
      const long long iy = std::atoll(y.c_str());
      if (ix != iy) return ix < iy ? -1 : 1;
    } else if (nx != ny) {
      return nx ? -1 : 1;  // numeric identifiers sort first
    } else if (int c = x.compare(y); c != 0) {
      return c < 0 ? -1 : 1;
    }
  }
  if (a.prerelease.size() != b.prerelease.size()) {
    return a.prerelease.size() < b.prerelease.size() ? -1 : 1;
  }
  return 0;
}

bool is_newer_version(std::string_view candidate, std::string_view current) {
  SemVer c = SemVer::parse(candidate);
  SemVer r = SemVer::parse(current);
  return c.valid && r.valid && SemVer::compare(c, r) > 0;
}

std::optional<ReleaseInfo> ReleaseInfo::from_json(std::string_view json, std::string* error) {
  auto fail = [error](const char* message) -> std::optional<ReleaseInfo> {
    if (error != nullptr) {
      *error = message;
    }
    return std::nullopt;
  };
  nlohmann::json doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded() || !doc.is_object()) {
    return fail("GitHub sent a reply zedit could not read.");
  }
  auto str = [&doc](const char* key) -> std::string {
    auto it = doc.find(key);
    return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
  };
  auto flag = [&doc](const char* key) {
    auto it = doc.find(key);
    return it != doc.end() && it->is_boolean() && it->get<bool>();
  };
  ReleaseInfo r;
  r.tag = str("tag_name");
  if (r.tag.empty()) {
    return fail("No published zedit release was found on GitHub.");
  }
  r.version = SemVer::parse(r.tag);
  if (!r.version.valid) {
    r.version = SemVer{};
    return fail("The latest release has a tag zedit doesn't understand.");
  }
  r.name = str("name");
  r.notes = str("body");
  r.html_url = str("html_url");
  r.draft = flag("draft");
  r.prerelease = flag("prerelease");
  if (auto assets = doc.find("assets"); assets != doc.end() && assets->is_array()) {
    for (const nlohmann::json& a : *assets) {
      if (!a.is_object()) continue;
      ReleaseAsset asset;
      if (auto n = a.find("name"); n != a.end() && n->is_string()) asset.name = n->get<std::string>();
      if (auto u = a.find("browser_download_url"); u != a.end() && u->is_string()) {
        asset.url = u->get<std::string>();
      }
      // Only a whole number in int64 range counts; anything else is 0.
      if (auto s = a.find("size"); s != a.end() && s->is_number_integer()) {
        if (s->is_number_unsigned()) {
          uint64_t size = s->get<uint64_t>();
          asset.size = size > static_cast<uint64_t>(INT64_MAX) ? 0 : static_cast<int64_t>(size);
        } else {
          asset.size = std::max<int64_t>(0, s->get<int64_t>());
        }
      }
      if (asset.valid()) {
        r.assets.push_back(std::move(asset));
      }
    }
  }
  return r;
}

namespace {

std::optional<ReleaseAsset> find_asset(const std::vector<ReleaseAsset>& assets,
                                       std::string_view name) {
  for (const ReleaseAsset& a : assets) {
    if (a.name == name) return a;
  }
  return std::nullopt;
}

}  // namespace

std::optional<ReleaseAsset> ReleaseInfo::deb_asset() const {
  return find_asset(assets, "zedit-" + version.to_string() + "-Linux-amd64.deb");
}

std::optional<ReleaseAsset> ReleaseInfo::dmg_asset() const {
  return find_asset(assets, "zedit-" + version.to_string() + "-Darwin.dmg");
}

std::optional<ReleaseAsset> ReleaseInfo::package_asset() const {
#if defined(__APPLE__)
  return dmg_asset();
#else
  return deb_asset();
#endif
}

std::optional<ReleaseAsset> ReleaseInfo::checksum_asset() const {
  return find_asset(assets, "SHA256SUMS");
}

std::optional<ReleaseAsset> ReleaseInfo::signature_asset() const {
  return find_asset(assets, "SHA256SUMS.minisig");
}

std::string expected_package_name(const SemVer& version) {
#if defined(__APPLE__)
  return "zedit-" + version.to_string() + "-Darwin.dmg";
#else
  return "zedit-" + version.to_string() + "-Linux-amd64.deb";
#endif
}

bool is_trusted_release_page_url(std::string_view url) {
  constexpr std::string_view kPrefix = "https://github.com/sbj-ee/zedit/releases/";
  if (url.substr(0, kPrefix.size()) != kPrefix) {
    return false;
  }
  // Nothing that could change what the prefix means: no userinfo, no control
  // characters or spaces, no backslashes.
  return std::none_of(url.begin(), url.end(), [](unsigned char c) {
    return c <= 0x20 || c == 0x7f || c == '\\' || c == '@';
  });
}

std::string checksum_for(std::string_view sha256sums, std::string_view file_name) {
  std::istringstream lines{std::string(sha256sums)};
  std::string raw;
  while (std::getline(lines, raw)) {
    std::string_view line = trim(raw);
    // "<64 hex>" then " " or "  " or " *" then the name.
    if (line.size() < 67 || !is_hex64(line.substr(0, 64)) || line[64] != ' ' ||
        (line[65] != ' ' && line[65] != '*')) {
      continue;
    }
    std::string_view name = trim(line.substr(66));
    if (name.substr(0, 2) == "./") {
      name.remove_prefix(2);
    }
    if (name == file_name) {
      return lower(line.substr(0, 64));
    }
  }
  return {};
}

ChecksumResult verify_checksum(const std::string& file_path, std::string_view sha256sums,
                               std::string_view file_name) {
  ChecksumResult r;
  r.expected = checksum_for(sha256sums, file_name);
  if (r.expected.empty()) {
    r.status = trim(sha256sums).empty() ? ChecksumResult::Status::BadFile
                                        : ChecksumResult::Status::NotListed;
    return r;
  }
  r.actual = sha256_file_hex(file_path);
  if (r.actual.empty()) {
    r.status = ChecksumResult::Status::Unreadable;
    return r;
  }
  r.status = r.actual == r.expected ? ChecksumResult::Status::Ok : ChecksumResult::Status::Mismatch;
  return r;
}

std::string ChecksumResult::message() const {
  switch (status) {
    case Status::Ok:
      return "SHA256 verified (" + actual + ").";
    case Status::Mismatch:
      return "The downloaded package does not match its published SHA256 checksum, so it was "
             "not installed.\nExpected: " +
             expected + "\nGot:      " + actual;
    case Status::NotListed:
      return "SHA256SUMS does not list the package, so it can't be verified and was not "
             "installed.";
    case Status::BadFile:
      return "The release's SHA256SUMS file is empty or unreadable, so the package was not "
             "installed.";
    case Status::Unreadable:
      break;
  }
  return "The downloaded package could not be read, so it was not installed.";
}

std::string UpdateState::default_path() {
  std::string dir = config_dir();
  return dir.empty() ? std::string() : dir + "/update_state";
}

UpdateState UpdateState::load(const std::string& path) {
  UpdateState st;
  std::ifstream file(path);
  std::string line;
  while (file && std::getline(file, line)) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key(trim(std::string_view(line).substr(0, eq)));
    std::string value(trim(std::string_view(line).substr(eq + 1)));
    if (key == "last_check") {
      if (is_numeric(value) && value.size() <= 15) {
        st.last_check = std::atoll(value.c_str());
      }
    } else if (key == "skipped_version") {
      st.skipped_version = value;
    } else if (key == "check_at_startup") {
      st.check_at_startup = value != "0";
    }
  }
  return st;
}

bool UpdateState::save(const std::string& path) const {
  if (path.empty()) return false;
  std::error_code ec;
  std::filesystem::path p(path);
  if (p.has_parent_path()) {
    std::filesystem::create_directories(p.parent_path(), ec);
  }
  std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return false;
    if (last_check.has_value()) out << "last_check=" << *last_check << "\n";
    out << "skipped_version=" << skipped_version << "\n";
    out << "check_at_startup=" << (check_at_startup ? "1" : "0") << "\n";
    out.flush();
    if (!out) return false;
  }
  std::filesystem::rename(tmp, path, ec);
  return !ec;
}

bool UpdatePolicy::should_check_at_startup(const UpdateState& state, int64_t now) {
  if (!state.check_at_startup) return false;
  if (!state.last_check.has_value()) return true;
  const int64_t age = now - *state.last_check;
  // A clock set back (negative age) also allows a check rather than blocking it for days.
  return age < 0 || age >= kMinSecondsBetweenChecks;
}

bool UpdatePolicy::should_offer(const ReleaseInfo& release, std::string_view current_version,
                                const UpdateState& state, bool manual) {
  if (release.draft || !is_newer_version(release.tag, current_version)) return false;
  if (manual || state.skipped_version.empty()) return true;
  // Skipped: quiet for that version (and anything older), offered again for newer ones.
  SemVer skipped = SemVer::parse(state.skipped_version);
  return skipped.valid ? SemVer::compare(release.version, skipped) > 0
                       : release.tag != state.skipped_version;
}

}  // namespace zedit::core
