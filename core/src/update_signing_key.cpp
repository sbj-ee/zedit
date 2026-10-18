// The public key that release SHA256SUMS files are signed with (minisign).
// Not secret. zedit refuses to install an update unless SHA256SUMS.minisig
// verifies against this key.
//
// Release key ID 32960BBA77B36F1F (minisign -G, October 2026); the same key
// signs zterminal's releases. To rotate it, see docs/RELEASING.md, "Signing key".
#include <mutex>

#include "zedit/core/update.hpp"

namespace zedit::core {

namespace {

constexpr char kUpdateSigningPublicKey[] =
    "RWQfb7N3uguWMhJ7jsE/Z0hZ7pDWHQ9OEeWSBTlN/R5bxQ8HWSMxp/l9";

std::mutex& test_key_mutex() {
  static std::mutex m;
  return m;
}

std::string& test_key() {
  static std::string k;
  return k;
}

}  // namespace

std::string update_signing_public_key() {
  std::lock_guard<std::mutex> lock(test_key_mutex());
  if (!test_key().empty()) {
    return test_key();
  }
  return kUpdateSigningPublicKey;
}

void set_update_signing_public_key_for_tests(std::string base64) {
  std::lock_guard<std::mutex> lock(test_key_mutex());
  test_key() = std::move(base64);
}

}  // namespace zedit::core
