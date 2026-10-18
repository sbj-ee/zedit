#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

#include "zedit/core/sha256.hpp"

using zedit::core::Sha256;
using zedit::core::sha256_file_hex;
using zedit::core::sha256_hex;

TEST_CASE("sha256 matches the FIPS 180-4 vectors", "[sha256]") {
  REQUIRE(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  REQUIRE(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  REQUIRE(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("sha256 of a million 'a' (many blocks)", "[sha256]") {
  std::string million(1000000, 'a');
  REQUIRE(sha256_hex(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256 streaming equals one-shot across odd chunk sizes", "[sha256]") {
  std::string data;
  for (int i = 0; i < 1000; ++i) data += static_cast<char>('a' + i % 26);
  const std::string whole = sha256_hex(data);
  for (size_t chunk : {1u, 3u, 63u, 64u, 65u, 200u}) {
    Sha256 h;
    for (size_t i = 0; i < data.size(); i += chunk) {
      h.update(data.data() + i, std::min(chunk, data.size() - i));
    }
    auto digest = h.finish();
    REQUIRE(zedit::core::to_hex(digest.data(), digest.size()) == whole);
  }
}

TEST_CASE("sha256_file_hex hashes a file and reports an unreadable one as empty", "[sha256]") {
  char path[] = "/tmp/zedit-sha-XXXXXX";
  int fd = mkstemp(path);
  REQUIRE(fd >= 0);
  close(fd);
  { std::ofstream(path, std::ios::binary) << "abc"; }
  REQUIRE(sha256_file_hex(path) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  std::remove(path);
  REQUIRE(sha256_file_hex(path).empty());
}
