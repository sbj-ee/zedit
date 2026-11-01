#include "zedit/core/file_io.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

namespace zedit::core {

void check_file_size_or_throw(uintmax_t size, const std::string& path) {
  if (size > kMaxFileSizeBytes) {
    throw FileTooLargeError("file too large to open (" + std::to_string(size / (1024 * 1024)) +
                             " MB, limit is " +
                             std::to_string(kMaxFileSizeBytes / (1024 * 1024)) + " MB): " + path);
  }
}

std::string read_file(const std::string& path) {
  std::error_code ec;
  uintmax_t size = std::filesystem::file_size(path, ec);
  if (!ec) {
    check_file_size_or_throw(size, path);
  }
  // If file_size() itself failed (nonexistent path, race, ...), fall
  // through to the ifstream open below, which produces the ordinary
  // "failed to open" FileIoError -- that path is unaffected by the size
  // check and keeps its existing "maybe this is a new file" meaning.

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw FileIoError("failed to open file for reading: " + path);
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

namespace {

std::string errno_text(int err) { return std::strerror(err); }

// Writes all of `content` to `fd`, then fsyncs. Returns 0 or an errno.
int write_all_and_sync(int fd, std::string_view content) {
  const char* p = content.data();
  size_t left = content.size();
  while (left > 0) {
    ssize_t n = ::write(fd, p, left);
    if (n < 0) {
      if (errno == EINTR) continue;
      return errno;
    }
    p += n;
    left -= static_cast<size_t>(n);
  }
  if (::fsync(fd) != 0 && errno != EINVAL && errno != ENOTSUP) {
    // EINVAL/ENOTSUP: the target (e.g. a pipe or /dev/null) can't be synced.
    return errno;
  }
  return 0;
}

// The old, non-atomic path: truncate the existing file and write into it.
// Used only for things that must not be replaced by a new regular file
// (devices, FIFOs) and when the directory doesn't allow creating the temp
// file (a writable file in a read-only directory). Every step is checked.
void write_in_place(const std::string& path, std::string_view content) {
  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  if (fd < 0) {
    throw FileIoError("failed to open file for writing: " + path + " (" + errno_text(errno) +
                      ")");
  }
  int err = write_all_and_sync(fd, content);
  if (::close(fd) != 0 && err == 0) {
    err = errno;
  }
  if (err != 0) {
    throw FileIoError("failed to write file: " + path + " (" + errno_text(err) + ")");
  }
}

void fsync_directory(const std::string& dir) {
  int dfd = ::open(dir.c_str(), O_RDONLY | O_CLOEXEC);
  if (dfd >= 0) {
    ::fsync(dfd);  // best effort: some filesystems refuse fsync on dirs
    ::close(dfd);
  }
}

}  // namespace

void write_file(const std::string& path, std::string_view content) {
  namespace fs = std::filesystem;
  // Save through a symlink into the file it points at (keeping the link),
  // like every other editor.
  std::string target = path;
  {
    char resolved[PATH_MAX];
    struct stat lst {};
    if (::lstat(path.c_str(), &lst) == 0 && S_ISLNK(lst.st_mode) &&
        ::realpath(path.c_str(), resolved) != nullptr) {
      target = resolved;
    }
  }

  struct stat st {};
  const bool exists = ::stat(target.c_str(), &st) == 0;
  if (exists && !S_ISREG(st.st_mode)) {
    write_in_place(target, content);  // /dev/..., FIFOs: never replace them
    return;
  }

  fs::path tpath(target);
  std::string dir = tpath.has_parent_path() ? tpath.parent_path().string() : std::string(".");
  std::string base = tpath.filename().string();

  // Sibling temp file, so the final rename stays on one filesystem and is
  // atomic: a failed or interrupted save leaves the original untouched.
  // O_EXCL with a fresh name each try; mode 0666 lets the umask apply for
  // a brand-new file, and an existing file's mode is copied below.
  std::random_device rd;
  std::string tmp;
  int fd = -1;
  int open_err = 0;
  for (int attempt = 0; attempt < 16 && fd < 0; ++attempt) {
    char suffix[16];
    std::snprintf(suffix, sizeof suffix, "%08x", static_cast<unsigned>(rd()));
    tmp = dir + "/." + base + ".zedit-" + suffix + ".tmp";
    fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    open_err = fd < 0 ? errno : 0;
    if (fd < 0 && open_err != EEXIST) break;
  }
  if (fd < 0) {
    if (exists && (open_err == EACCES || open_err == EPERM || open_err == EROFS)) {
      // Can't create files next to it, but the file itself may be writable.
      write_in_place(target, content);
      return;
    }
    throw FileIoError("failed to open file for writing: " + path + " (" +
                      errno_text(open_err) + ")");
  }

  int err = 0;
  if (exists) {
    // Keep the original's permission bits (and owner/group when allowed).
    if (::fchmod(fd, st.st_mode & 07777) != 0) err = errno;
    if (::fchown(fd, st.st_uid, st.st_gid) != 0) {
      // Not permitted for other users' files; the mode bits still match.
    }
  }
  if (err == 0) err = write_all_and_sync(fd, content);
  if (::close(fd) != 0 && err == 0) err = errno;
  if (err == 0 && ::rename(tmp.c_str(), target.c_str()) != 0) err = errno;
  if (err != 0) {
    ::unlink(tmp.c_str());
    throw FileIoError("failed to write file: " + path + " (" + errno_text(err) + ")");
  }
  fsync_directory(dir);
}

}  // namespace zedit::core
