#include "zedit/core/update_check.hpp"

#include "zedit/core/update.hpp"

namespace zedit::core {

std::optional<UpdateInfo> parse_newer_version(std::string_view current_version,
                                               std::string_view release_json) {
  std::optional<ReleaseInfo> release = ReleaseInfo::from_json(release_json);
  if (!release || release->draft || !is_newer_version(release->tag, current_version)) {
    return std::nullopt;
  }
  UpdateInfo info;
  info.version = release->tag;
  info.url = release->html_url;
  return info;
}

}  // namespace zedit::core
