#include "zedit/core/update_check.hpp"

#include <array>

#include <nlohmann/json.hpp>

namespace zedit::core {

namespace {

std::array<int, 3> parse_semver(std::string_view s) {
  if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) {
    s.remove_prefix(1);
  }
  std::array<int, 3> parts{0, 0, 0};
  size_t idx = 0;
  for (int i = 0; i < 3 && idx <= s.size(); ++i) {
    size_t dot = s.find('.', idx);
    std::string_view part =
        s.substr(idx, dot == std::string_view::npos ? std::string_view::npos : dot - idx);
    int value = 0;
    for (char c : part) {
      if (c < '0' || c > '9') {
        break;
      }
      value = value * 10 + (c - '0');
    }
    parts[static_cast<size_t>(i)] = value;
    if (dot == std::string_view::npos) {
      break;
    }
    idx = dot + 1;
  }
  return parts;
}

}  // namespace

std::optional<UpdateInfo> parse_newer_version(std::string_view current_version,
                                               std::string_view release_json) {
  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(release_json);
  } catch (const nlohmann::json::parse_error&) {
    return std::nullopt;
  }
  if (!parsed.is_object() || !parsed.contains("tag_name") || !parsed["tag_name"].is_string()) {
    return std::nullopt;
  }

  std::string tag = parsed["tag_name"].get<std::string>();
  if (parse_semver(tag) <= parse_semver(current_version)) {
    return std::nullopt;
  }

  UpdateInfo info;
  info.version = std::move(tag);
  info.url = parsed.value("html_url", "");
  return info;
}

}  // namespace zedit::core
