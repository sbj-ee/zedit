#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace zedit::core {

enum class BufferSource { Original, Added };

struct Piece {
  BufferSource source;
  size_t start;
  size_t length;
};

// A piece-table text buffer: the original file content is never mutated in
// place, edits are appended to a separate buffer, and the document is the
// concatenation of small (source, start, length) spans over the two. This
// makes undo/redo a matter of recording piece-list splices rather than
// content copies, and keeps large-file loads to a single read with no
// upfront copy.
class PieceTable {
 public:
  PieceTable();
  explicit PieceTable(std::string original_content);

  size_t size() const { return total_length_; }
  bool empty() const { return total_length_ == 0; }

  std::string to_string() const;
  std::string text_range(size_t start, size_t length) const;

  void insert(size_t offset, std::string_view text);
  void erase(size_t offset, size_t length);

  size_t line_count() const;
  size_t line_start_offset(size_t line) const;
  size_t offset_to_line(size_t offset) const;

  struct LineCol {
    size_t line;
    size_t col;
  };
  LineCol offset_to_line_col(size_t offset) const;
  size_t line_col_to_offset(size_t line, size_t col) const;
  std::string line_text(size_t line) const;

  // A snapshot is just the piece list, not the underlying content: original_
  // is immutable after construction and added_ only ever grows, so restoring
  // pieces_ instantly reverts document content without touching either
  // buffer. This makes undo/redo cheap regardless of document size.
  struct Snapshot {
    std::vector<Piece> pieces;
    size_t total_length = 0;
  };
  Snapshot snapshot() const;
  void restore(Snapshot snap);

 private:
  std::string original_;
  std::string added_;
  std::vector<Piece> pieces_;
  size_t total_length_ = 0;

  mutable bool line_index_dirty_ = true;
  mutable std::vector<size_t> line_starts_;

  std::string_view piece_view(const Piece& p) const;
  size_t split_at(size_t offset);
  void ensure_line_index() const;
};

}  // namespace zedit::core
