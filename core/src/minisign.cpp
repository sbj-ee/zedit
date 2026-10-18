#include "zedit/core/minisign.hpp"

#include <cstring>

extern "C" {
#include <monocypher-ed25519.h>
#include <monocypher.h>
}

namespace zedit::core {

namespace {

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' ||
                        s.front() == '\n')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' ||
                        s.back() == '\n')) {
    s.remove_suffix(1);
  }
  return s;
}

std::vector<std::string_view> split_lines(std::string_view text) {
  std::vector<std::string_view> lines;
  size_t start = 0;
  while (start <= text.size()) {
    size_t nl = text.find('\n', start);
    std::string_view line =
        text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    lines.push_back(line);
    if (nl == std::string_view::npos) {
      break;
    }
    start = nl + 1;
  }
  while (!lines.empty() && lines.back().empty()) {
    lines.pop_back();
  }
  return lines;
}

// Ed25519 (SHA-512) verification: true when `sig` is valid for `msg`.
bool ed25519_valid(const uint8_t* sig, const uint8_t* public_key, const uint8_t* msg,
                   size_t size) {
  return crypto_ed25519_check(sig, public_key, msg, size) == 0;
}

}  // namespace

std::optional<std::vector<uint8_t>> base64_decode(std::string_view text) {
  text = trim(text);
  if (text.empty() || text.size() % 4 != 0) {
    return std::nullopt;
  }
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::vector<uint8_t> out;
  out.reserve(text.size() / 4 * 3);
  for (size_t i = 0; i < text.size(); i += 4) {
    int v[4];
    int pad = 0;
    for (size_t j = 0; j < 4; ++j) {
      char c = text[i + j];
      if (c == '=') {
        // Padding only in the last quartet, last one or two places.
        if (i + 4 != text.size() || j < 2) {
          return std::nullopt;
        }
        ++pad;
        v[j] = 0;
      } else {
        if (pad > 0) {
          return std::nullopt;
        }
        v[j] = value(c);
        if (v[j] < 0) {
          return std::nullopt;
        }
      }
    }
    uint32_t triple = (static_cast<uint32_t>(v[0]) << 18) | (static_cast<uint32_t>(v[1]) << 12) |
                      (static_cast<uint32_t>(v[2]) << 6) | static_cast<uint32_t>(v[3]);
    out.push_back(static_cast<uint8_t>(triple >> 16));
    if (pad < 2) out.push_back(static_cast<uint8_t>(triple >> 8));
    if (pad < 1) out.push_back(static_cast<uint8_t>(triple));
  }
  return out;
}

std::optional<MinisignPublicKey> MinisignPublicKey::parse(std::string_view text,
                                                          std::string* error) {
  auto fail = [error](const char* why) -> std::optional<MinisignPublicKey> {
    if (error != nullptr) {
      *error = why;
    }
    return std::nullopt;
  };
  std::string_view b64;
  for (std::string_view line : split_lines(text)) {
    line = trim(line);
    if (line.empty() || line.substr(0, 18) == "untrusted comment:") {
      continue;
    }
    b64 = line;
    break;
  }
  auto bin = base64_decode(b64);
  if (!bin || bin->size() != 42 || (*bin)[0] != 'E' || (*bin)[1] != 'd') {
    return fail("not a minisign Ed25519 public key");
  }
  MinisignPublicKey k;
  std::memcpy(k.key_id.data(), bin->data() + 2, 8);
  std::memcpy(k.key.data(), bin->data() + 10, 32);
  return k;
}

std::string MinisignPublicKey::key_id_hex() const {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  std::string s;
  for (size_t i = 8; i-- > 0;) {
    s.push_back(kDigits[key_id[i] >> 4]);
    s.push_back(kDigits[key_id[i] & 0x0f]);
  }
  return s;
}

MinisignResult verify_minisign(std::string_view message, std::string_view signature_file,
                               const MinisignPublicKey& key) {
  MinisignResult r;
  const auto lines = split_lines(signature_file);
  constexpr std::string_view kTrusted = "trusted comment: ";
  constexpr const char* kBadFormat = "the signature file is not in minisign format";
  if (lines.size() != 4 || lines[0].substr(0, 18) != "untrusted comment:" ||
      lines[2].substr(0, kTrusted.size()) != kTrusted) {
    r.error = kBadFormat;
    return r;
  }
  auto sig = base64_decode(lines[1]);
  if (!sig || sig->size() != 74 || (*sig)[0] != 'E' || ((*sig)[1] != 'D' && (*sig)[1] != 'd')) {
    r.error = kBadFormat;
    return r;
  }
  if (std::memcmp(sig->data() + 2, key.key_id.data(), 8) != 0) {
    r.error = "the signature was made with a different key";
    return r;
  }
  const uint8_t* sig_bytes = sig->data() + 10;
  const auto* msg = reinterpret_cast<const uint8_t*>(message.data());
  bool good = false;
  if ((*sig)[1] == 'D') {
    std::array<uint8_t, 64> hash{};
    crypto_blake2b(hash.data(), hash.size(), msg, message.size());
    good = ed25519_valid(sig_bytes, key.key.data(), hash.data(), hash.size());
  } else {
    good = ed25519_valid(sig_bytes, key.key.data(), msg, message.size());
  }
  if (!good) {
    r.error = "the signature does not match the file";
    return r;
  }
  std::string_view trusted = lines[2].substr(kTrusted.size());
  auto global = base64_decode(lines[3]);
  if (!global || global->size() != 64) {
    r.error = kBadFormat;
    return r;
  }
  std::string signed_part(reinterpret_cast<const char*>(sig_bytes), 64);
  signed_part.append(trusted);
  if (!ed25519_valid(global->data(), key.key.data(),
                     reinterpret_cast<const uint8_t*>(signed_part.data()), signed_part.size())) {
    r.error = "the signature's trusted comment was altered";
    return r;
  }
  r.ok = true;
  r.trusted_comment = std::string(trusted);
  return r;
}

}  // namespace zedit::core
