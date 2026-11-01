#include "zedit/core/recovery.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <type_traits>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>

#include "zedit/core/file_io.hpp"
#include "zedit/core/sha256.hpp"

namespace zedit::core {
namespace {

constexpr char kMagic[8] = {'Z', 'E', 'D', 'I', 'T', 'S', 'W', 'P'};
constexpr uint32_t kVersion = 1;

template <typename T>
void append_le(std::vector<uint8_t>& out, T value) {
  static_assert(std::is_integral_v<T>);
  for (size_t i = 0; i < sizeof(T); ++i) {
    out.push_back(static_cast<uint8_t>((static_cast<uint64_t>(value) >> (8 * i)) & 0xff));
  }
}

template <typename T>
bool read_le(const std::vector<uint8_t>& buf, size_t& off, T& out) {
  if (off + sizeof(T) > buf.size()) {
    return false;
  }
  uint64_t v = 0;
  for (size_t i = 0; i < sizeof(T); ++i) {
    v |= static_cast<uint64_t>(buf[off + i]) << (8 * i);
  }
  out = static_cast<T>(v);
  off += sizeof(T);
  return true;
}

int64_t wall_time_ns() {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

void delete_quiet(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

}  // namespace

std::string default_swap_dir() {
  if (const char* env = std::getenv("ZEDIT_SWAP_DIR"); env != nullptr && *env != '\0') {
    return std::string(env);
  }
#if defined(__APPLE__)
  if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::string(home) + "/Library/Application Support/zedit/swap";
  }
#else
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && *xdg != '\0') {
    return std::string(xdg) + "/zedit/swap";
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::string(home) + "/.cache/zedit/swap";
  }
#endif
  // No HOME at all: a per-user directory rather than a fixed, guessable
  // /tmp/zedit/swap another user could create first. write_swap_atomic()
  // still refuses it unless it's ours and private.
  return "/tmp/zedit-" + std::to_string(static_cast<unsigned long>(::geteuid())) + "/swap";
}

std::string absolute_path_for_swap(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  std::error_code ec;
  std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
  if (!ec) {
    return canonical.string();
  }
  std::filesystem::path abs = std::filesystem::absolute(path, ec);
  if (!ec) {
    return abs.lexically_normal().string();
  }
  return path;
}

std::string swap_path_for(const std::string& abs_path) {
  return (std::filesystem::path(default_swap_dir()) / (sha256_hex(abs_path) + ".swp")).string();
}

Baseline baseline_for_path(const std::string& path) {
  Baseline b;
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) {
    return b;
  }
#if defined(__APPLE__)
  b.mtime_ns = static_cast<int64_t>(st.st_mtimespec.tv_sec) * 1000000000LL +
               static_cast<int64_t>(st.st_mtimespec.tv_nsec);
#else
  b.mtime_ns = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000LL +
               static_cast<int64_t>(st.st_mtim.tv_nsec);
#endif
  b.size = static_cast<uint64_t>(st.st_size);
  b.inode = static_cast<uint64_t>(st.st_ino);
  return b;
}

namespace {

// Swap files hold unsaved text, which may be private. The directory must
// be a real directory (not a symlink) owned by us; it's created 0700 and
// tightened to 0700 if an older zedit made it with default permissions.
bool ensure_private_dir(const std::filesystem::path& dir) {
  std::error_code ec;
  if (dir.has_parent_path()) {
    std::filesystem::create_directories(dir.parent_path(), ec);
  }
  if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
    return false;
  }
  struct stat st {};
  if (::lstat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != ::geteuid()) {
    return false;
  }
  if ((st.st_mode & 0077) != 0 && ::chmod(dir.c_str(), 0700) != 0) {
    return false;
  }
  return true;
}

bool write_all(int fd, const uint8_t* data, size_t size) {
  while (size > 0) {
    ssize_t n = ::write(fd, data, size);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    data += n;
    size -= static_cast<size_t>(n);
  }
  return true;
}

}  // namespace

bool write_swap_atomic(const std::string& abs_path, std::string_view content,
                       const Baseline& baseline) {
  if (abs_path.empty()) {
    return false;
  }
  if (content.size() > kMaxFileSizeBytes) {
    return false;
  }

  std::filesystem::path dir = default_swap_dir();
  std::error_code ec;
  if (!ensure_private_dir(dir)) {
    return false;
  }

  const std::filesystem::path swp = swap_path_for(abs_path);
  const std::filesystem::path tmp = swp.string() + ".tmp";

  std::vector<uint8_t> bytes;
  bytes.reserve(64 + abs_path.size() + content.size());
  bytes.insert(bytes.end(), kMagic, kMagic + 8);
  append_le<uint32_t>(bytes, kVersion);
  append_le<uint32_t>(bytes, static_cast<uint32_t>(abs_path.size()));
  bytes.insert(bytes.end(), abs_path.begin(), abs_path.end());
  append_le<int64_t>(bytes, baseline.mtime_ns);
  append_le<uint64_t>(bytes, baseline.size);
  append_le<uint64_t>(bytes, baseline.inode);
  append_le<int64_t>(bytes, wall_time_ns());
  append_le<uint32_t>(bytes, 0);  // cursor_line reserved
  append_le<uint32_t>(bytes, 0);  // cursor_col reserved
  append_le<uint64_t>(bytes, static_cast<uint64_t>(content.size()));
  bytes.insert(bytes.end(), content.begin(), content.end());

  {
    // 0600, created fresh (a stale .tmp from a crash is removed first), and
    // never through a symlink.
    delete_quiet(tmp);
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
      return false;
    }
    bool ok = write_all(fd, bytes.data(), bytes.size()) && ::fsync(fd) == 0;
    if (::close(fd) != 0) {
      ok = false;
    }
    if (!ok) {
      delete_quiet(tmp);
      return false;
    }
  }

  std::filesystem::rename(tmp, swp, ec);
  if (ec) {
    delete_quiet(tmp);
    return false;
  }
  return true;
}

std::optional<SwapPayload> read_swap(const std::string& abs_path) {
  if (abs_path.empty()) {
    return std::nullopt;
  }
  const std::filesystem::path swp = swap_path_for(abs_path);
  std::ifstream in(swp, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }

  std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
  auto fail = [&]() -> std::optional<SwapPayload> {
    delete_quiet(swp);
    return std::nullopt;
  };

  if (buf.size() < 8 + 4 + 4) {
    return fail();
  }
  if (std::memcmp(buf.data(), kMagic, 8) != 0) {
    return fail();
  }

  size_t off = 8;
  uint32_t version = 0;
  if (!read_le(buf, off, version)) {
    return fail();
  }
  if (version != kVersion) {
    // Written by a different (likely newer) zedit: not ours to judge, and
    // deleting it would throw away that version's recovery data.
    return std::nullopt;
  }
  uint32_t path_len = 0;
  if (!read_le(buf, off, path_len)) {
    return fail();
  }
  if (off + path_len > buf.size()) {
    return fail();
  }
  SwapPayload payload;
  payload.path.assign(reinterpret_cast<const char*>(buf.data() + off), path_len);
  off += path_len;

  if (!read_le(buf, off, payload.baseline.mtime_ns) ||
      !read_le(buf, off, payload.baseline.size) ||
      !read_le(buf, off, payload.baseline.inode) || !read_le(buf, off, payload.saved_at_ns) ||
      !read_le(buf, off, payload.cursor_line) || !read_le(buf, off, payload.cursor_col)) {
    return fail();
  }
  uint64_t content_len = 0;
  if (!read_le(buf, off, content_len)) {
    return fail();
  }
  if (content_len > kMaxFileSizeBytes || off + content_len != buf.size()) {
    return fail();
  }
  payload.content.assign(reinterpret_cast<const char*>(buf.data() + off),
                         static_cast<size_t>(content_len));
  return payload;
}

void clear_swap(const std::string& abs_path) {
  if (abs_path.empty()) {
    return;
  }
  delete_quiet(swap_path_for(abs_path));
}

std::optional<RecoveryOffer> consider_recovery(const std::string& abs_path,
                                               std::string_view disk_content) {
  std::optional<SwapPayload> swap = read_swap(abs_path);
  if (!swap) {
    return std::nullopt;
  }
  if (swap->content == disk_content) {
    clear_swap(abs_path);
    return std::nullopt;
  }
  // MVP: same Recover/Discard prompt for external-edit / clock-skew cases.
  return RecoveryOffer{abs_path, std::move(swap->content)};
}

void RecoveryDebouncer::arm(std::chrono::milliseconds delay) {
  due_ = std::chrono::steady_clock::now() + delay;
}

bool RecoveryDebouncer::poll() {
  if (!due_) {
    return false;
  }
  if (std::chrono::steady_clock::now() < *due_) {
    return false;
  }
  due_.reset();
  return true;
}

void RecoveryDebouncer::clear() { due_.reset(); }

}  // namespace zedit::core
