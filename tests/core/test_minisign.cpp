// Minisign verification (SHA256SUMS.minisig): output of the real minisign tool,
// tampering, wrong keys, malformed files.
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "../helpers/minisign_test_signer.hpp"
#include "zedit/core/minisign.hpp"
#include "zedit/core/update.hpp"

using zedit::core::base64_decode;
using zedit::core::MinisignPublicKey;
using zedit::core::MinisignResult;
using zedit::core::verify_minisign;
using zedit::test::MinisignTestKey;

namespace {

std::string fixture(const std::string& name) {
  std::ifstream in(std::string(ZEDIT_TEST_DATA "/minisign/") + name, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void replace_all(std::string& s, const std::string& from, const std::string& to) {
  for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) {
    s.replace(pos, from.size(), to);
  }
}

// zterminal's release public key (key ID 32960BBA77B36F1F). Not zedit's release key: it is here
// only because tests/data/minisign/release-key/ holds the real signature zterminal's pipeline
// published, which this key verifies.
constexpr char kZterminalReleasePublicKey[] = "RWQfb7N3uguWMhJ7jsE/Z0hZ7pDWHQ9OEeWSBTlN/R5bxQ8HWSMxp/l9";

}  // namespace

TEST_CASE("base64_decode is strict", "[minisign]") {
  auto ok = base64_decode("YWJj");
  REQUIRE(ok.has_value());
  REQUIRE(std::string(ok->begin(), ok->end()) == "abc");
  REQUIRE(base64_decode(" YWJjZA==\n").has_value());  // surrounding whitespace ignored
  REQUIRE_FALSE(base64_decode("").has_value());
  REQUIRE_FALSE(base64_decode("YWJ").has_value());       // bad length
  REQUIRE_FALSE(base64_decode("YW!j").has_value());      // outside the alphabet
  REQUIRE_FALSE(base64_decode("YWJj YWJj").has_value());  // interior whitespace
}

TEST_CASE("verifies the output of the real minisign tool", "[minisign]") {
  auto key = MinisignPublicKey::parse(fixture("test.pub"));
  REQUIRE(key.has_value());
  REQUIRE(key->key_id_hex() == "452E2115C846828B");  // as in the .pub comment
  const std::string sums = fixture("SHA256SUMS");
  REQUIRE_FALSE(sums.empty());
  MinisignResult r = verify_minisign(sums, fixture("SHA256SUMS.minisig"), *key);
  INFO(r.error);
  REQUIRE(r.ok);
  REQUIRE(r.trusted_comment == "timestamp:0 file:SHA256SUMS hashed");

  // Legacy (non-prehashed "Ed") signatures too.
  MinisignResult legacy =
      verify_minisign(fixture("SHA256SUMS.legacy"), fixture("SHA256SUMS.legacy.minisig"), *key);
  INFO(legacy.error);
  REQUIRE(legacy.ok);

  // One changed byte in the file.
  std::string evil = sums;
  evil[0] = evil[0] == 'a' ? 'c' : 'a';
  REQUIRE_FALSE(verify_minisign(evil, fixture("SHA256SUMS.minisig"), *key).ok);

  // The bare base64 line works as a key as well.
  REQUIRE(MinisignPublicKey::parse("RWSLgkbIFSEuRS5xAEv9bP6Ma6NWSf7wuuDwLKeT++2GUiloG7OAZjVS"));
}

TEST_CASE("verifies signatures made with a generated test key", "[minisign]") {
  MinisignTestKey k(1);
  auto key = MinisignPublicKey::parse(k.public_key_base64());
  REQUIRE(key.has_value());
  const std::string msg = "0123  zedit-1.2.0-Linux-amd64.deb\n";
  REQUIRE(verify_minisign(msg, k.sign(msg), *key).ok);
  REQUIRE(verify_minisign(msg, k.sign(msg, "legacy", false), *key).ok);
  std::string crlf = k.sign(msg);
  replace_all(crlf, "\n", "\r\n");
  REQUIRE(verify_minisign(msg, crlf, *key).ok);  // CRLF tolerated
}

TEST_CASE("tampering is rejected, with a reason", "[minisign]") {
  MinisignTestKey k(1);
  MinisignTestKey other(2);
  const auto key = *MinisignPublicKey::parse(k.public_key_base64());
  const std::string msg = "abc  zedit-1.2.0-Linux-amd64.deb\n";
  const std::string sig = k.sign(msg);

  SECTION("a different file") {
    REQUIRE(verify_minisign(msg + "x", sig, key).error == "the signature does not match the file");
  }
  SECTION("an edited trusted comment (the global signature covers it)") {
    std::string edited = sig;
    replace_all(edited, "file:SHA256SUMS", "file:SHA256SUMX");
    REQUIRE(verify_minisign(msg, edited, key).error ==
            "the signature's trusted comment was altered");
  }
  SECTION("the untrusted comment may change freely") {
    std::string untrusted = sig;
    replace_all(untrusted, "signature from minisign secret key", "anything");
    REQUIRE(verify_minisign(msg, untrusted, key).ok);
  }
  SECTION("signed by another key") {
    REQUIRE(verify_minisign(msg, other.sign(msg), key).error ==
            "the signature was made with a different key");
  }
  SECTION("another key that claims our key id") {
    other.key_id = k.key_id;
    REQUIRE(verify_minisign(msg, other.sign(msg), key).error ==
            "the signature does not match the file");
  }
  SECTION("prehash flag flipped (ED <-> Ed)") {
    // Re-sign the same bytes as a legacy signature but present them as ED.
    std::string legacy = k.sign(msg, "timestamp:0 file:SHA256SUMS hashed", /*prehash=*/false);
    // The second line is base64("Ed"|id|sig"); rewriting its prefix to "ED" must fail.
    size_t nl1 = legacy.find('\n');
    size_t nl2 = legacy.find('\n', nl1 + 1);
    auto raw = *base64_decode(legacy.substr(nl1 + 1, nl2 - nl1 - 1));
    raw[1] = 'D';
    std::string flipped = legacy.substr(0, nl1 + 1) + zedit::test::base64_encode(raw.data(), raw.size()) +
                          legacy.substr(nl2);
    REQUIRE_FALSE(verify_minisign(msg, flipped, key).ok);
  }
}

TEST_CASE("malformed signature files are refused", "[minisign]") {
  MinisignTestKey k(1);
  const auto key = *MinisignPublicKey::parse(k.public_key_base64());
  const std::string line74 = zedit::test::base64_encode(
      reinterpret_cast<const uint8_t*>(std::string(74, 'E').data()), 74);
  for (const std::string& sig : {
           std::string(),
           std::string("hello\nworld\n"),
           std::string("untrusted comment: x\nRUQ=\ntrusted comment: y\n"),
           std::string("untrusted comment: x\n!!!!\ntrusted comment: y\nAAAA\n"),
           std::string("untrusted comment: x\nRUQAAAA=\ntrusted comment: y\nAAAA\n"),
           "untrusted comment: x\n" + line74 + "\ncomment: y\nAAAA\n",
       }) {
    MinisignResult r = verify_minisign("x", sig, key);
    INFO(sig);
    REQUIRE_FALSE(r.ok);
    REQUIRE_FALSE(r.error.empty());
  }
}

TEST_CASE("the release key compiled into zedit is a valid minisign key", "[minisign]") {
  // If this is wrong every in-app update is refused.
  zedit::core::set_update_signing_public_key_for_tests("");
  const std::string key = zedit::core::update_signing_public_key();
  REQUIRE_FALSE(key.empty());
  std::string error;
  auto parsed = MinisignPublicKey::parse(key, &error);
  INFO(error);
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->key_id_hex() == "DBC46B23C3AF0EF3");
  // zedit's own key, not zterminal's.
  REQUIRE(key == "RWTzDq/DI2vE2xMeF01QWTxP2lwwlxGL9M1UxQE4EFV5kVmTF84wwyi2");
  REQUIRE(parsed->key_id_hex() != "32960BBA77B36F1F");
}

TEST_CASE("a real published release signature verifies (zterminal's key, as a test constant)",
          "[minisign]") {
  // zterminal's published v1.6.0 SHA256SUMS: a real signature made by real minisign with a real
  // release key, checked against that key's public half (a constant above, not the key compiled
  // into zedit). Proves the verifier reads real-world minisign output end to end.
  auto key = MinisignPublicKey::parse(kZterminalReleasePublicKey);
  REQUIRE(key.has_value());
  const std::string sums = fixture("release-key/SHA256SUMS");
  const std::string sig = fixture("release-key/SHA256SUMS.minisig");
  REQUIRE_FALSE(sums.empty());
  MinisignResult r = verify_minisign(sums, sig, *key);
  INFO(r.error);
  REQUIRE(r.ok);
  REQUIRE(r.trusted_comment == "zterminal v1.6.0 SHA256SUMS");
  std::string evil = sums;
  evil[0] = evil[0] == 'a' ? 'b' : 'a';
  REQUIRE_FALSE(verify_minisign(evil, sig, *key).ok);
  // ... and zedit's own release key does not accept it.
  zedit::core::set_update_signing_public_key_for_tests("");
  auto own = MinisignPublicKey::parse(zedit::core::update_signing_public_key());
  REQUIRE(own.has_value());
  REQUIRE_FALSE(verify_minisign(sums, sig, *own).ok);
}

TEST_CASE("bad public keys are refused", "[minisign]") {
  REQUIRE_FALSE(MinisignPublicKey::parse("").has_value());
  REQUIRE_FALSE(MinisignPublicKey::parse("not base64 !!").has_value());
  const std::string no_ed(42, 'x');
  REQUIRE_FALSE(MinisignPublicKey::parse(zedit::test::base64_encode(
                                             reinterpret_cast<const uint8_t*>(no_ed.data()), 42))
                    .has_value());
  const std::string shorty = "Ed" + std::string(30, 'x');
  REQUIRE_FALSE(MinisignPublicKey::parse(zedit::test::base64_encode(
                                             reinterpret_cast<const uint8_t*>(shorty.data()),
                                             shorty.size()))
                    .has_value());
}
