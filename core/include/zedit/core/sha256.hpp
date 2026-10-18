#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace zedit::core {

// SHA-256 (FIPS 180-4), streaming. Used to check a downloaded update
// against the SHA256SUMS the release signature covers.
class Sha256 {
 public:
  Sha256();
  void update(const void* data, size_t size);
  // Finishes the hash; the object must not be updated afterwards.
  std::array<uint8_t, 32> finish();

 private:
  void compress(const uint8_t* block);

  std::array<uint32_t, 8> state_;
  std::array<uint8_t, 64> buffer_{};
  size_t buffer_size_ = 0;
  uint64_t total_bytes_ = 0;
};

std::string to_hex(const uint8_t* data, size_t size);
// Lower-case hex SHA-256 of `data`.
std::string sha256_hex(std::string_view data);
// Lower-case hex SHA-256 of the file at `path`; empty if it can't be read.
std::string sha256_file_hex(const std::string& path);

}  // namespace zedit::core
