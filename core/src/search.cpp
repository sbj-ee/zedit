#include "zedit/core/search.hpp"

#include <string>

namespace zedit::core {

std::optional<size_t> search_forward(const PieceTable& buf, size_t from_offset,
                                      std::string_view pattern) {
  if (pattern.empty()) {
    return std::nullopt;
  }
  std::string content = buf.to_string();
  if (content.empty()) {
    return std::nullopt;
  }
  size_t start = std::min(from_offset + 1, content.size());
  size_t pos = content.find(pattern, start);
  if (pos == std::string::npos) {
    pos = content.find(pattern, 0);
  }
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  return pos;
}

std::optional<size_t> search_backward(const PieceTable& buf, size_t from_offset,
                                       std::string_view pattern) {
  if (pattern.empty()) {
    return std::nullopt;
  }
  std::string content = buf.to_string();
  if (content.empty()) {
    return std::nullopt;
  }
  if (from_offset > 0) {
    size_t pos = content.rfind(pattern, from_offset - 1);
    if (pos != std::string::npos) {
      return pos;
    }
  }
  size_t pos = content.rfind(pattern, content.size());
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  return pos;
}

}  // namespace zedit::core
