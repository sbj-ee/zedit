#include "zedit/core/recovery.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <type_traits>
#include <vector>

#include <sys/stat.h>

#include "zedit/core/file_io.hpp"

namespace zedit::core {
namespace {

constexpr char kMagic[8] = {'Z', 'E', 'D', 'I', 'T', 'S', 'W', 'P'};
constexpr uint32_t kVersion = 1;

// Minimal public-domain SHA-256 (based on Brad Conte's implementation).
class Sha256 {
 public:
  Sha256() { reset(); }

  void update(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
      data_[datalen_++] = data[i];
      if (datalen_ == 64) {
        transform();
        bitlen_ += 512;
        datalen_ = 0;
      }
    }
  }

  void update(std::string_view s) {
    update(reinterpret_cast<const uint8_t*>(s.data()), s.size());
  }

  std::array<uint8_t, 32> finalize() {
    size_t i = datalen_;
    data_[i++] = 0x80;
    if (datalen_ < 56) {
      while (i < 56) data_[i++] = 0x00;
    } else {
      while (i < 64) data_[i++] = 0x00;
      transform();
      std::memset(data_.data(), 0, 56);
    }
    bitlen_ += static_cast<uint64_t>(datalen_) * 8;
    data_[63] = static_cast<uint8_t>(bitlen_);
    data_[62] = static_cast<uint8_t>(bitlen_ >> 8);
    data_[61] = static_cast<uint8_t>(bitlen_ >> 16);
    data_[60] = static_cast<uint8_t>(bitlen_ >> 24);
    data_[59] = static_cast<uint8_t>(bitlen_ >> 32);
    data_[58] = static_cast<uint8_t>(bitlen_ >> 40);
    data_[57] = static_cast<uint8_t>(bitlen_ >> 48);
    data_[56] = static_cast<uint8_t>(bitlen_ >> 56);
    transform();

    std::array<uint8_t, 32> hash{};
    for (i = 0; i < 4; ++i) {
      hash[i] = (state_[0] >> (24 - i * 8)) & 0xff;
      hash[i + 4] = (state_[1] >> (24 - i * 8)) & 0xff;
      hash[i + 8] = (state_[2] >> (24 - i * 8)) & 0xff;
      hash[i + 12] = (state_[3] >> (24 - i * 8)) & 0xff;
      hash[i + 16] = (state_[4] >> (24 - i * 8)) & 0xff;
      hash[i + 20] = (state_[5] >> (24 - i * 8)) & 0xff;
      hash[i + 24] = (state_[6] >> (24 - i * 8)) & 0xff;
      hash[i + 28] = (state_[7] >> (24 - i * 8)) & 0xff;
    }
    return hash;
  }

 private:
  void reset() {
    datalen_ = 0;
    bitlen_ = 0;
    state_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
              0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  }

  static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

  void transform() {
    static constexpr uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
        0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
        0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

    uint32_t m[64];
    for (int i = 0, j = 0; i < 16; ++i, j += 4) {
      m[i] = (static_cast<uint32_t>(data_[j]) << 24) |
             (static_cast<uint32_t>(data_[j + 1]) << 16) |
             (static_cast<uint32_t>(data_[j + 2]) << 8) |
             (static_cast<uint32_t>(data_[j + 3]));
    }
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
      uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
      m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
      uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t temp1 = h + S1 + ch + k[i] + m[i];
      uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temp2 = S0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<uint8_t, 64> data_{};
  uint32_t datalen_ = 0;
  uint64_t bitlen_ = 0;
  std::array<uint32_t, 8> state_{};
};

std::string to_hex(const std::array<uint8_t, 32>& hash) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(64, '\0');
  for (size_t i = 0; i < 32; ++i) {
    out[i * 2] = kHex[(hash[i] >> 4) & 0xf];
    out[i * 2 + 1] = kHex[hash[i] & 0xf];
  }
  return out;
}

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
  return "/tmp/zedit/swap";
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
  Sha256 sha;
  sha.update(abs_path);
  return (std::filesystem::path(default_swap_dir()) / (to_hex(sha.finalize()) + ".swp")).string();
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
  std::filesystem::create_directories(dir, ec);
  if (ec) {
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
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
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
  if (!read_le(buf, off, version) || version != kVersion) {
    return fail();
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
