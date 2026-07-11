#include "zedit/core/piece_table.hpp"

#include <algorithm>
#include <utility>

namespace zedit::core {

PieceTable::PieceTable() = default;

PieceTable::PieceTable(std::string original_content)
    : original_(std::move(original_content)) {
  total_length_ = original_.size();
  if (total_length_ > 0) {
    pieces_.push_back(Piece{BufferSource::Original, 0, total_length_});
  }
}

std::string_view PieceTable::piece_view(const Piece& p) const {
  const std::string& buf =
      (p.source == BufferSource::Original) ? original_ : added_;
  return std::string_view(buf).substr(p.start, p.length);
}

size_t PieceTable::split_at(size_t offset) {
  size_t pos = 0;
  for (size_t i = 0; i < pieces_.size(); ++i) {
    Piece& p = pieces_[i];
    if (offset == pos) {
      return i;
    }
    if (offset < pos + p.length) {
      size_t local = offset - pos;
      Piece right{p.source, p.start + local, p.length - local};
      p.length = local;
      pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                      right);
      return i + 1;
    }
    pos += p.length;
  }
  return pieces_.size();
}

void PieceTable::insert(size_t offset, std::string_view text) {
  if (text.empty()) {
    return;
  }
  offset = std::min(offset, total_length_);

  size_t added_start = added_.size();
  added_.append(text);
  Piece new_piece{BufferSource::Added, added_start, text.size()};

  size_t idx = split_at(offset);
  pieces_.insert(pieces_.begin() + static_cast<std::ptrdiff_t>(idx),
                  new_piece);

  total_length_ += text.size();
  line_index_dirty_ = true;
}

void PieceTable::erase(size_t offset, size_t length) {
  if (offset >= total_length_ || length == 0) {
    return;
  }
  length = std::min(length, total_length_ - offset);

  size_t idx = split_at(offset);
  size_t remaining = length;
  while (remaining > 0 && idx < pieces_.size()) {
    Piece& p = pieces_[idx];
    if (p.length <= remaining) {
      remaining -= p.length;
      pieces_.erase(pieces_.begin() + static_cast<std::ptrdiff_t>(idx));
    } else {
      p.start += remaining;
      p.length -= remaining;
      remaining = 0;
    }
  }

  total_length_ -= length;
  line_index_dirty_ = true;
}

std::string PieceTable::text_range(size_t start, size_t length) const {
  start = std::min(start, total_length_);
  length = std::min(length, total_length_ - start);

  std::string result;
  result.reserve(length);
  size_t end = start + length;
  size_t pos = 0;
  for (const Piece& p : pieces_) {
    if (pos >= end) {
      break;
    }
    size_t piece_end = pos + p.length;
    if (piece_end > start) {
      size_t local_start = (start > pos) ? (start - pos) : 0;
      size_t local_end = std::min(p.length, end - pos);
      if (local_start < local_end) {
        std::string_view view = piece_view(p);
        result.append(view.substr(local_start, local_end - local_start));
      }
    }
    pos = piece_end;
  }
  return result;
}

std::string PieceTable::to_string() const { return text_range(0, total_length_); }

void PieceTable::ensure_line_index() const {
  if (!line_index_dirty_) {
    return;
  }
  line_starts_.clear();
  line_starts_.push_back(0);
  size_t offset = 0;
  for (const Piece& p : pieces_) {
    std::string_view view = piece_view(p);
    for (char c : view) {
      ++offset;
      if (c == '\n') {
        line_starts_.push_back(offset);
      }
    }
  }
  line_index_dirty_ = false;
}

size_t PieceTable::line_count() const {
  ensure_line_index();
  return line_starts_.size();
}

size_t PieceTable::line_start_offset(size_t line) const {
  ensure_line_index();
  if (line_starts_.empty()) {
    return 0;
  }
  line = std::min(line, line_starts_.size() - 1);
  return line_starts_[line];
}

size_t PieceTable::offset_to_line(size_t offset) const {
  ensure_line_index();
  auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
  size_t line = static_cast<size_t>(std::distance(line_starts_.begin(), it));
  return line == 0 ? 0 : line - 1;
}

PieceTable::LineCol PieceTable::offset_to_line_col(size_t offset) const {
  offset = std::min(offset, total_length_);
  size_t line = offset_to_line(offset);
  return LineCol{line, offset - line_start_offset(line)};
}

size_t PieceTable::line_col_to_offset(size_t line, size_t col) const {
  ensure_line_index();
  if (line_starts_.empty()) {
    return 0;
  }
  line = std::min(line, line_starts_.size() - 1);
  size_t line_begin = line_starts_[line];
  size_t line_end = (line + 1 < line_starts_.size())
                         ? line_starts_[line + 1] - 1
                         : total_length_;
  return std::min(line_begin + col, line_end);
}

std::string PieceTable::line_text(size_t line) const {
  ensure_line_index();
  if (line_starts_.empty()) {
    return {};
  }
  line = std::min(line, line_starts_.size() - 1);
  size_t start = line_starts_[line];
  size_t end = (line + 1 < line_starts_.size()) ? line_starts_[line + 1] - 1
                                                 : total_length_;
  return text_range(start, end - start);
}

}  // namespace zedit::core
