#pragma once
// Test-only minisign signer (Monocypher), so tests can sign fixtures with a
// keypair derived from a fixed *test* seed. zedit itself only verifies; none of
// these keys is, or resembles, the release key.
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <monocypher-ed25519.h>
#include <monocypher.h>
}

#include "zedit/core/minisign.hpp"

namespace zedit::test {

inline std::string base64_encode(const uint8_t* data, size_t size) {
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < size; i += 3) {
    uint32_t n = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < size) n |= static_cast<uint32_t>(data[i + 1]) << 8;
    if (i + 2 < size) n |= data[i + 2];
    out.push_back(kAlphabet[(n >> 18) & 63]);
    out.push_back(kAlphabet[(n >> 12) & 63]);
    out.push_back(i + 1 < size ? kAlphabet[(n >> 6) & 63] : '=');
    out.push_back(i + 2 < size ? kAlphabet[n & 63] : '=');
  }
  return out;
}

struct MinisignTestKey {
  std::array<uint8_t, 64> secret{};
  std::array<uint8_t, 32> pub{};
  std::array<uint8_t, 8> key_id{};

  // `which` picks one of several distinct throwaway keys.
  explicit MinisignTestKey(uint8_t which) {
    uint8_t seed[32];
    for (size_t i = 0; i < sizeof seed; ++i) {
      seed[i] = static_cast<uint8_t>(0x5a ^ which ^ (i * 7));
    }
    crypto_ed25519_key_pair(secret.data(), pub.data(), seed);
    for (size_t i = 0; i < key_id.size(); ++i) {
      key_id[i] = static_cast<uint8_t>(0xc0 + which + i);
    }
  }

  std::string public_key_base64() const {
    std::vector<uint8_t> raw = {'E', 'd'};
    raw.insert(raw.end(), key_id.begin(), key_id.end());
    raw.insert(raw.end(), pub.begin(), pub.end());
    return base64_encode(raw.data(), raw.size());
  }

  std::string sign(const std::string& message,
                   const std::string& trusted = "timestamp:0 file:SHA256SUMS hashed",
                   bool prehash = true) const {
    uint8_t sig[64];
    if (prehash) {
      uint8_t hash[64];
      crypto_blake2b(hash, sizeof hash, reinterpret_cast<const uint8_t*>(message.data()),
                     message.size());
      crypto_ed25519_sign(sig, secret.data(), hash, sizeof hash);
    } else {
      crypto_ed25519_sign(sig, secret.data(),
                          reinterpret_cast<const uint8_t*>(message.data()), message.size());
    }
    std::vector<uint8_t> line = {static_cast<uint8_t>('E'), static_cast<uint8_t>(prehash ? 'D' : 'd')};
    line.insert(line.end(), key_id.begin(), key_id.end());
    line.insert(line.end(), sig, sig + 64);
    std::vector<uint8_t> global_message(sig, sig + 64);
    global_message.insert(global_message.end(), trusted.begin(), trusted.end());
    uint8_t global[64];
    crypto_ed25519_sign(global, secret.data(), global_message.data(), global_message.size());
    return "untrusted comment: signature from minisign secret key\n" +
           base64_encode(line.data(), line.size()) + "\ntrusted comment: " + trusted + "\n" +
           base64_encode(global, 64) + "\n";
  }
};

}  // namespace zedit::test
