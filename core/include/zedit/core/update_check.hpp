#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace zedit::core {

struct UpdateInfo {
  std::string version;  // e.g. "v0.2.0", straight from the release's tag_name
  std::string url;       // release page URL, empty if the JSON didn't have one
};

// Parses a GitHub "get latest release" API response
// (https://api.github.com/repos/<owner>/<repo>/releases/latest) and
// compares its tag_name against current_version. Both are loosely parsed
// as major.minor.patch (tolerating an optional leading 'v' and missing
// trailing components, which are treated as 0), so "v0.2" beats "0.1.9".
// Returns the release info if it's newer than current_version, nullopt if
// it isn't, or if release_json is malformed/missing tag_name (e.g. no
// releases published yet, in which case GitHub returns a 404 body with no
// tag_name at all).
std::optional<UpdateInfo> parse_newer_version(std::string_view current_version,
                                               std::string_view release_json);

}  // namespace zedit::core
