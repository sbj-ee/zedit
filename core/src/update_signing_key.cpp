// The public key that release SHA256SUMS files are signed with (minisign).
// Not secret. zedit refuses to install an update unless SHA256SUMS.minisig
// verifies against this key.
//
// Release key ID DBC46B23C3AF0EF3 (minisign -G on the maintainer's machine,
// October 2026): zedit's own release key. The matching secret key lives only on
// the maintainer's machine and in this repository's MINISIGN_SECRET_KEY /
// MINISIGN_PASSWORD Actions secrets. To rotate it, see docs/RELEASING.md,
// "Signing key".
#include <mutex>

#include "zedit/core/update.hpp"

namespace zedit::core {

namespace {

constexpr char kUpdateSigningPublicKey[] =
    "RWTzDq/DI2vE2xMeF01QWTxP2lwwlxGL9M1UxQE4EFV5kVmTF84wwyi2";

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
