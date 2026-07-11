#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace zedit::core {

class FileIoError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// A file that exists and is readable, but too big to safely load into an
// in-memory piece table. Deliberately a *subclass* of FileIoError but
// callers that treat "can't read this file" as "start a blank new file"
// (Editor::open_buffer, main.cpp's make_editor -- matching vim's ":e
// newfile.txt") must catch this one first and handle it separately:
// silently substituting an empty buffer for a too-large file would still
// leave the real file's name attached to that empty buffer, so a
// subsequent save would overwrite the original (huge) file with nothing.
class FileTooLargeError : public FileIoError {
 public:
  using FileIoError::FileIoError;
};

// Generous on purpose (the user just wants a backstop, not a tight
// limit) -- large enough that no legitimate text/code file should ever
// hit it, small enough to protect against accidentally opening something
// that was never meant to be loaded whole into memory (a multi-gigabyte
// log file, a binary, etc).
inline constexpr uintmax_t kMaxFileSizeBytes = 256ULL * 1024 * 1024;  // 256 MB

// Throws FileTooLargeError if `size` exceeds kMaxFileSizeBytes. Exposed
// separately from read_file() so tests can exercise the limit logic
// without needing to actually create a 256MB+ file on disk.
void check_file_size_or_throw(uintmax_t size, const std::string& path);

std::string read_file(const std::string& path);
void write_file(const std::string& path, std::string_view content);

}  // namespace zedit::core
