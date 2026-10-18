// In-app updater, pure logic: version compare, release JSON, asset selection,
// checksums, policy and state. (The download/verify/install flow against a
// local fake release server is test_update_service.cpp.)
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

#include "zedit/core/sha256.hpp"
#include "zedit/core/update.hpp"
#include "zedit/core/version.hpp"

using namespace zedit::core;

namespace {

std::string fixture(const std::string& name) {
  std::ifstream in(std::string(ZEDIT_TEST_DATA "/update/") + name, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct TempDir {
  std::filesystem::path path;
  TempDir() {
    char templ[] = "/tmp/zedit-update-test-XXXXXX";
    path = mkdtemp(templ);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  std::string file(const std::string& name) const { return (path / name).string(); }
};

}  // namespace

TEST_CASE("semver parsing", "[update]") {
  REQUIRE(SemVer::parse("1.2.3").valid);
  REQUIRE(SemVer::parse("v1.2.3").to_string() == "1.2.3");
  REQUIRE(SemVer::parse("1.2").to_string() == "1.2.0");
  REQUIRE(SemVer::parse("1").to_string() == "1.0.0");
  REQUIRE(SemVer::parse("1.0.0-rc.1").to_string() == "1.0.0-rc.1");
  REQUIRE(SemVer::parse("1.0.0~rc1").prerelease == std::vector<std::string>{"rc1"});
  REQUIRE(SemVer::parse("1.0.0+build5").to_string() == "1.0.0");
  for (const char* bad : {"", "v", "abc", "1.2.3.4", "1.x.3", "1.0.0-", "1.0.0-a..b", "-1.0.0",
                          "99999999999.0.0"}) {
    INFO(bad);
    REQUIRE_FALSE(SemVer::parse(bad).valid);
  }
}

TEST_CASE("version comparison", "[update]") {
  REQUIRE(is_newer_version("1.8.0", "1.7.4"));
  REQUIRE(is_newer_version("v1.10.0", "1.9.9"));  // numeric, not lexical
  REQUIRE(is_newer_version("2.0.0", "1.99.99"));
  REQUIRE_FALSE(is_newer_version("1.7.4", "1.7.4"));
  REQUIRE_FALSE(is_newer_version("1.7.3", "1.7.4"));
  REQUIRE(is_newer_version("1.0.0", "1.0.0-rc.1"));  // a release beats its prerelease
  REQUIRE_FALSE(is_newer_version("1.0.0-rc.1", "1.0.0"));
  REQUIRE(is_newer_version("1.0.0-rc.2", "1.0.0-rc.1"));
  REQUIRE(is_newer_version("1.0.0-rc.10", "1.0.0-rc.9"));
  REQUIRE(is_newer_version("1.0.0-rc.1", "1.0.0-beta.9"));
  REQUIRE(is_newer_version("1.0.0-alpha", "1.0.0-1"));  // numeric identifiers sort first
  REQUIRE_FALSE(is_newer_version("1.0.0+b2", "1.0.0+b1"));  // build metadata ignored
  REQUIRE_FALSE(is_newer_version("garbage", "1.0.0"));
  REQUIRE_FALSE(is_newer_version("1.0.0", "garbage"));
}

TEST_CASE("the built-in version is valid semver", "[update]") {
  REQUIRE(SemVer::parse(version_string()).valid);
}

TEST_CASE("release JSON parses", "[update]") {
  std::string error;
  auto r = ReleaseInfo::from_json(fixture("release-0.10.0.json"), &error);
  INFO(error);
  REQUIRE(r.has_value());
  REQUIRE(r->tag == "v0.10.0");
  REQUIRE(r->version.to_string() == "0.10.0");
  REQUIRE(r->name == "zedit 0.10.0");
  REQUIRE(r->html_url == "https://github.com/sbj-ee/zedit/releases/tag/v0.10.0");
  REQUIRE_FALSE(r->notes.empty());
  REQUIRE_FALSE(r->draft);
  REQUIRE_FALSE(r->prerelease);
  REQUIRE(r->assets.size() == 4);
  REQUIRE(ReleaseInfo::from_json(fixture("release-rc.json"))->prerelease);
}

TEST_CASE("release JSON that isn't a release is refused", "[update]") {
  std::string error;
  REQUIRE_FALSE(ReleaseInfo::from_json("", &error).has_value());
  REQUIRE_FALSE(error.empty());
  REQUIRE_FALSE(ReleaseInfo::from_json("[]").has_value());
  REQUIRE_FALSE(ReleaseInfo::from_json("not json").has_value());
  // GitHub's 404 body has no tag_name.
  REQUIRE_FALSE(ReleaseInfo::from_json(R"({"message":"Not Found"})", &error).has_value());
  REQUIRE(error.find("No published") != std::string::npos);
  REQUIRE_FALSE(ReleaseInfo::from_json(R"({"tag_name":"nightly"})", &error).has_value());
  REQUIRE_FALSE(ReleaseInfo::from_json(R"({"tag_name":7})").has_value());
}

TEST_CASE("hostile release fields are tolerated, not trusted", "[update]") {
  auto r = ReleaseInfo::from_json(R"({
    "tag_name": "v1.0.0",
    "body": 5, "name": null, "draft": "yes",
    "assets": [
      7, null, {},
      {"name": "a", "size": 99999999999999999999999, "browser_download_url": "u"},
      {"name": "b", "size": -5, "browser_download_url": "u"},
      {"name": "c", "size": 1.5, "browser_download_url": "u"},
      {"name": "d", "size": "9", "browser_download_url": "u"},
      {"name": "e", "size": 12, "browser_download_url": 3},
      {"name": 4, "size": 12, "browser_download_url": "u"}
    ]})");
  REQUIRE(r.has_value());
  REQUIRE(r->notes.empty());
  REQUIRE(r->name.empty());
  REQUIRE_FALSE(r->draft);  // only a real boolean counts
  REQUIRE(r->assets.size() == 4);  // a, b, c, d: the ones with a name and url
  for (const ReleaseAsset& a : r->assets) REQUIRE(a.size == 0);
}

TEST_CASE("asset selection", "[update]") {
  auto r = ReleaseInfo::from_json(fixture("release-0.10.0.json"));
  REQUIRE(r.has_value());
  REQUIRE(r->deb_asset()->name == "zedit-0.10.0-Linux-amd64.deb");
  REQUIRE(r->dmg_asset()->name == "zedit-0.10.0-Darwin.dmg");
  REQUIRE(r->checksum_asset()->name == "SHA256SUMS");
  REQUIRE(r->signature_asset()->name == "SHA256SUMS.minisig");
  REQUIRE(r->package_asset()->name == expected_package_name(r->version));
  REQUIRE_FALSE(r->deb_asset()->url.empty());

  auto no_deb = ReleaseInfo::from_json(fixture("release-no-deb.json"));
  REQUIRE(no_deb.has_value());
  REQUIRE_FALSE(no_deb->deb_asset().has_value());  // a different package name doesn't count
  REQUIRE_FALSE(no_deb->signature_asset().has_value());  // an unsigned release says so
  REQUIRE(no_deb->checksum_asset().has_value());

  auto rc = ReleaseInfo::from_json(fixture("release-rc.json"));
  REQUIRE(rc->deb_asset()->name == "zedit-1.0.0-rc.1-Linux-amd64.deb");
}

TEST_CASE("only github.com/sbj-ee/zedit release pages are opened", "[update]") {
  REQUIRE(is_trusted_release_page_url("https://github.com/sbj-ee/zedit/releases/tag/v1.8.0"));
  REQUIRE_FALSE(is_trusted_release_page_url(""));
  REQUIRE_FALSE(is_trusted_release_page_url("http://github.com/sbj-ee/zedit/releases/tag/v1"));
  REQUIRE_FALSE(is_trusted_release_page_url("https://github.com/sbj-ee/zedit-evil/releases/x"));
  REQUIRE_FALSE(is_trusted_release_page_url("https://github.com/sbj-ee/zedit/releases/x y"));
  REQUIRE_FALSE(is_trusted_release_page_url("https://github.com/sbj-ee/zedit/releases/@evil.com"));
  REQUIRE_FALSE(is_trusted_release_page_url("https://evil.com/https://github.com/sbj-ee/zedit/releases/"));
  REQUIRE_FALSE(is_trusted_release_page_url("file:///etc/passwd"));
}

TEST_CASE("checksums", "[update][checksum]") {
  TempDir dir;
  { std::ofstream(dir.file("zedit-1.0.0-Linux-amd64.deb"), std::ios::binary) << "package bytes"; }
  const std::string good = sha256_hex("package bytes");
  const std::string name = "zedit-1.0.0-Linux-amd64.deb";
  const std::string path = dir.file(name);

  SECTION("text and binary style lines, any case, ./ prefix") {
    REQUIRE(checksum_for(good + "  " + name + "\n", name) == good);
    REQUIRE(checksum_for(good + " *" + name + "\n", name) == good);
    std::string upper = good;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    REQUIRE(checksum_for(upper + "  ./" + name + "\r\n", name) == good);
    REQUIRE(checksum_for("zzz  other\n" + good + "  " + name + "\n", name) == good);
  }
  SECTION("not listed, malformed or partial matches") {
    REQUIRE(checksum_for(good + "  other.deb\n", name).empty());
    REQUIRE(checksum_for(good.substr(1) + "  " + name + "\n", name).empty());  // 63 digits
    REQUIRE(checksum_for(good + "  x" + name + "\n", name).empty());           // other name
    REQUIRE(checksum_for("", name).empty());
  }
  SECTION("verify_checksum statuses") {
    REQUIRE(verify_checksum(path, good + "  " + name + "\n", name).ok());
    auto bad = verify_checksum(path, std::string(64, 'a') + "  " + name + "\n", name);
    REQUIRE(bad.status == ChecksumResult::Status::Mismatch);
    REQUIRE(bad.message().find("does not match") != std::string::npos);
    REQUIRE(verify_checksum(path, good + "  other\n", name).status ==
            ChecksumResult::Status::NotListed);
    REQUIRE(verify_checksum(path, "  \n", name).status == ChecksumResult::Status::BadFile);
    REQUIRE(verify_checksum(dir.file("missing"), good + "  " + name + "\n", name).status ==
            ChecksumResult::Status::Unreadable);
  }
}

TEST_CASE("update policy: once a day, switch, skip", "[update][policy]") {
  UpdateState s;
  const int64_t now = 1'800'000'000;
  SECTION("startup check") {
    REQUIRE(UpdatePolicy::should_check_at_startup(s, now));  // never checked
    s.last_check = now - 3600;
    REQUIRE_FALSE(UpdatePolicy::should_check_at_startup(s, now));
    s.last_check = now - UpdatePolicy::kMinSecondsBetweenChecks;
    REQUIRE(UpdatePolicy::should_check_at_startup(s, now));
    s.last_check = now + 99999;  // clock went backwards
    REQUIRE(UpdatePolicy::should_check_at_startup(s, now));
    s.check_at_startup = false;
    REQUIRE_FALSE(UpdatePolicy::should_check_at_startup(s, now));
  }
  SECTION("offering") {
    auto release = *ReleaseInfo::from_json(R"({"tag_name":"v1.8.0"})");
    REQUIRE(UpdatePolicy::should_offer(release, "1.7.4", s, false));
    REQUIRE_FALSE(UpdatePolicy::should_offer(release, "1.8.0", s, true));  // not newer
    REQUIRE_FALSE(UpdatePolicy::should_offer(release, "1.9.0", s, true));
    auto draft = release;
    draft.draft = true;
    REQUIRE_FALSE(UpdatePolicy::should_offer(draft, "1.7.4", s, true));
    s.skipped_version = "v1.8.0";
    REQUIRE_FALSE(UpdatePolicy::should_offer(release, "1.7.4", s, false));  // skipped: quiet
    REQUIRE(UpdatePolicy::should_offer(release, "1.7.4", s, true));         // the menu always offers
    auto newer = *ReleaseInfo::from_json(R"({"tag_name":"v1.8.1"})");
    REQUIRE(UpdatePolicy::should_offer(newer, "1.7.4", s, false));  // a newer one is offered again
  }
}

TEST_CASE("update state round-trips and survives garbage", "[update][policy]") {
  TempDir dir;
  const std::string path = dir.file("sub/dir/update_state");
  UpdateState s;
  s.last_check = 1'700'000'123;
  s.skipped_version = "v1.2.3";
  s.check_at_startup = false;
  REQUIRE(s.save(path));  // creates the folders
  UpdateState loaded = UpdateState::load(path);
  REQUIRE(loaded.last_check == 1'700'000'123);
  REQUIRE(loaded.skipped_version == "v1.2.3");
  REQUIRE_FALSE(loaded.check_at_startup);

  UpdateState missing = UpdateState::load(dir.file("nope"));
  REQUIRE_FALSE(missing.last_check.has_value());
  REQUIRE(missing.check_at_startup);  // defaults: on

  { std::ofstream(dir.file("junk")) << "last_check=banana\n=\nnoequals\ncheck_at_startup=0\n\x01\x02"; }
  UpdateState junk = UpdateState::load(dir.file("junk"));
  REQUIRE_FALSE(junk.last_check.has_value());
  REQUIRE_FALSE(junk.check_at_startup);
  REQUIRE_FALSE(UpdateState{}.save(""));
}
