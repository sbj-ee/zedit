#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zedit::core {

// Strict standard-alphabet base64 (with padding). Surrounding whitespace is
// ignored; anything else outside the alphabet makes it nullopt.
std::optional<std::vector<uint8_t>> base64_decode(std::string_view text);

// Verification of minisign (https://jedisct1.github.io/minisign/) detached
// signatures, as used for a release's SHA256SUMS (SHA256SUMS.minisig).
// Verification only: zedit never holds a secret key. Ed25519 and BLAKE2b come
// from the vendored Monocypher.
//
// Public key: base64("Ed" | key id (8) | Ed25519 public key (32)), optionally
// after an "untrusted comment:" line (the .pub file as minisign writes it).
// Signature file, four lines:
//   untrusted comment: <anything, not authenticated>
//   base64("ED" or "Ed" | key id (8) | signature (64))
//   trusted comment: <text>
//   base64(global signature (64))
// "ED" (minisign's default) signs BLAKE2b-512(file); legacy "Ed" signs the file.
// The global signature covers signature (64) | <text>, so the trusted comment
// can't be swapped.
struct MinisignPublicKey {
  std::array<uint8_t, 8> key_id{};
  std::array<uint8_t, 32> key{};

  static std::optional<MinisignPublicKey> parse(std::string_view text,
                                                std::string* error = nullptr);
  // As minisign prints it (upper-case, little-endian order).
  std::string key_id_hex() const;
};

struct MinisignResult {
  bool ok = false;
  std::string error;           // why not, for the user
  std::string trusted_comment;  // when ok
};

MinisignResult verify_minisign(std::string_view message, std::string_view signature_file,
                               const MinisignPublicKey& key);

}  // namespace zedit::core
