// The updater's whole flow (check, download, signature, checksum, install)
// against a local fake release server, with the real `curl`. The installer is
// replaced by a harmless shell command that records what it was asked to
// install: nothing is ever installed on the machine running the tests.
#include <catch2/catch_test_macros.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <fstream>
#include <utility>
#include <iterator>
#include <string>
#include <thread>

#include <imgui.h>

#include "../helpers/fake_http_server.hpp"
#include "../helpers/minisign_test_signer.hpp"
#include "update_ui.hpp"
#include "zedit/core/editor.hpp"
#include "zedit/core/sha256.hpp"
#include "zedit/core/update.hpp"
#include "zedit/core/update_service.hpp"

using namespace zedit::core;
namespace fs = std::filesystem;
using zedit::test::FakeHttpServer;
using zedit::test::MinisignTestKey;

namespace {

std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool wait_until(const std::function<bool()>& done, int seconds = 30) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  while (!done()) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return true;
}

size_t entries_in(const fs::path& dir) {
  size_t n = 0;
  for (auto it = fs::directory_iterator(dir); it != fs::directory_iterator(); ++it) ++n;
  return n;
}

// One fake release v1.8.0 (signed with a test key) behind a fake server.
struct Fixture {
  FakeHttpServer server;
  fs::path root;
  fs::path work;  // the updater's temp_root: must be empty again after a job
  std::string record;  // the stand-in installer writes the package path here
  MinisignTestKey key{1};
  std::string package_name;
  std::string package_bytes = "pretend this is a .deb";
  std::string sums;
  std::string tag;

  explicit Fixture(std::string release_tag = "v1.8.0") : tag(std::move(release_tag)) {
    char templ[] = "/tmp/zedit-svc-test-XXXXXX";
    root = mkdtemp(templ);
    work = root / "work";
    fs::create_directories(work);
    record = (root / "installed-path").string();
    package_name = expected_package_name(SemVer::parse(tag));
    set_update_signing_public_key_for_tests(key.public_key_base64());
    publish();
  }
  ~Fixture() {
    set_update_signing_public_key_for_tests("");
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  std::string url(const std::string& path) const { return server.base() + path; }

  // (Re)publishes the release; the arguments let a test break one part.
  void publish(bool with_signature_asset = true, bool with_package_asset = true) {
    sums = sha256_hex(package_bytes) + "  " + package_name + "\n";
    std::string assets;
    if (with_package_asset) {
      assets += R"({"name":")" + package_name + R"(","size":)" + std::to_string(package_bytes.size()) +
                R"(,"browser_download_url":")" + url("/download/" + package_name) + R"("},)";
    }
    assets += R"({"name":"SHA256SUMS","size":)" + std::to_string(sums.size()) +
              R"(,"browser_download_url":")" + url("/download/SHA256SUMS") + R"("})";
    if (with_signature_asset) {
      assets += R"(,{"name":"SHA256SUMS.minisig","size":300,"browser_download_url":")" +
                url("/download/SHA256SUMS.minisig") + R"("})";
    }
    server.set("/latest", {200, R"({"tag_name":")" + tag + R"(","name":"zedit )" + tag +
                                    R"(","html_url":"https://github.com/sbj-ee/zedit/releases/tag/)" + tag +
                                    R"(","assets":[)" + assets + "]}", {}, false});
    // GitHub serves downloads through a redirect to its CDN; so do we.
    server.set("/download/" + package_name, {302, "", {{"Location", "/cdn/" + package_name}}, false});
    server.set("/cdn/" + package_name, {200, package_bytes, {}, false});
    server.set("/download/SHA256SUMS", {200, sums, {}, false});
    server.set("/download/SHA256SUMS.minisig", {200, key.sign(sums), {}, false});
  }

  UpdateConfig config(int installer_exit = 0) const {
    UpdateConfig c;
    c.current_version = "1.7.4";
    c.api_url = url("/latest");
    c.https_only = false;  // the local server is plain http
    c.temp_root = work.string();
    c.connect_timeout_s = 5;
    c.check_timeout_s = 10;
    c.installer_override = {"/bin/sh", "-c",
                            "printf '%s' \"$0\" > '" + record + "'; exit " + std::to_string(installer_exit)};
    return c;
  }

  bool installer_ran() const { return fs::exists(record); }
  bool package_requested() const { return server.count("/download/" + package_name) > 0; }
};

UpdateCheckResult check(UpdateService& s, bool manual = true) {
  s.start_check(manual);
  REQUIRE(wait_until([&] { return !s.busy(); }));
  auto r = s.poll_check();
  REQUIRE(r.has_value());
  return *r;
}

ReleaseInfo fetch_release(UpdateService& s) {
  UpdateCheckResult r = check(s);
  REQUIRE(r.outcome == UpdateCheckResult::Outcome::Release);
  return r.release;
}

UpdateInstallResult install(UpdateService& s, const ReleaseInfo& release) {
  s.start_install(release);
  REQUIRE(wait_until([&] { return !s.busy(); }));
  auto r = s.poll_install();
  REQUIRE(r.has_value());
  return *r;
}

}  // namespace

TEST_CASE("check: finds the latest release", "[update-service]") {
  Fixture f;
  UpdateService s(f.config());
  UpdateCheckResult r = check(s, /*manual=*/true);
  REQUIRE(r.outcome == UpdateCheckResult::Outcome::Release);
  REQUIRE(r.manual);
  REQUIRE(r.release.tag == "v1.8.0");
  REQUIRE(r.release.package_asset().has_value());
  REQUIRE(r.release.signature_asset().has_value());
  REQUIRE_FALSE(check(s, /*manual=*/false).manual);
  REQUIRE_FALSE(s.poll_check().has_value());  // a result is handed out once
}

TEST_CASE("check: failures are reported, never thrown", "[update-service]") {
  Fixture f;

  SECTION("no release published (404)") {
    f.server.remove("/latest");
    UpdateService s(f.config());
    REQUIRE(check(s).outcome == UpdateCheckResult::Outcome::NoRelease);
  }
  SECTION("server error") {
    f.server.set("/latest", {500, "oops", {}, false});
    UpdateService s(f.config());
    UpdateCheckResult r = check(s);
    REQUIRE(r.outcome == UpdateCheckResult::Outcome::Error);
    REQUIRE(r.error.find("500") != std::string::npos);
  }
  SECTION("rate limit") {
    f.server.set("/latest", {403, "{}", {{"x-ratelimit-remaining", "0"}, {"x-ratelimit-reset", "1900000000"}}, false});
    UpdateService s(f.config());
    UpdateCheckResult r = check(s);
    REQUIRE(r.outcome == UpdateCheckResult::Outcome::Error);
    REQUIRE(r.rate_limited);
    REQUIRE(r.error.find("rate limit") != std::string::npos);
  }
  SECTION("a forbidden that isn't a rate limit") {
    f.server.set("/latest", {403, "{}", {}, false});
    UpdateService s(f.config());
    UpdateCheckResult r = check(s);
    REQUIRE_FALSE(r.rate_limited);
    REQUIRE(r.error.find("refused") != std::string::npos);
  }
  SECTION("a reply that isn't a release") {
    f.server.set("/latest", {200, "<html>captive portal</html>", {}, false});
    UpdateService s(f.config());
    REQUIRE(check(s).outcome == UpdateCheckResult::Outcome::Error);
  }
  SECTION("nothing listening") {
    UpdateConfig c = f.config();
    c.api_url = "http://127.0.0.1:1/latest";
    UpdateService s(c);
    UpdateCheckResult r = check(s);
    REQUIRE(r.outcome == UpdateCheckResult::Outcome::Error);
    REQUIRE(r.error.find("reach") != std::string::npos);
  }
  SECTION("curl missing") {
    UpdateConfig c = f.config();
    c.curl = "/nonexistent/curl";
    UpdateService s(c);
    UpdateCheckResult r = check(s);
    REQUIRE(r.outcome == UpdateCheckResult::Outcome::Error);
    REQUIRE(r.error.find("curl") != std::string::npos);
  }
  SECTION("plain http is refused when https is required (the production setting)") {
    UpdateConfig c = f.config();
    c.https_only = true;
    UpdateService s(c);
    REQUIRE(check(s).outcome == UpdateCheckResult::Outcome::Error);
    REQUIRE(f.server.requests().empty());
  }
  REQUIRE(entries_in(f.work) == 0);
}

TEST_CASE("install: a signed, matching release is handed to the installer", "[update-service]") {
  Fixture f;
  UpdateService s(f.config());
  ReleaseInfo release = fetch_release(s);
  UpdateInstallResult r = install(s, release);
  INFO(r.message);
  REQUIRE(r.outcome == UpdateInstallResult::Outcome::Installed);
  REQUIRE(r.tag == "v1.8.0");
  REQUIRE(r.kept_package.empty());

  // What the installer got: an absolute path to a private copy of the package.
  const std::string installed = slurp(f.record);
  REQUIRE(fs::path(installed).is_absolute());
  REQUIRE(fs::path(installed).filename() == f.package_name);

  // Order: the signed sums and their signature are fetched before the package.
  const int sums_at = f.server.index_of("/download/SHA256SUMS");
  const int sig_at = f.server.index_of("/download/SHA256SUMS.minisig");
  const int pkg_at = f.server.index_of("/download/" + f.package_name);
  REQUIRE(sums_at >= 0);
  REQUIRE(sig_at >= 0);
  REQUIRE(pkg_at > sig_at);
  REQUIRE(pkg_at > sums_at);
  // Everything is cleaned up after a successful install.
  REQUIRE(entries_in(f.work) == 0);
}

#ifndef __APPLE__  // the probe uses GNU stat/sha256sum
TEST_CASE("install: the installer sees a private (0700) folder and the verified bytes",
          "[update-service]") {
  Fixture f;
  UpdateConfig c = f.config();
  const std::string probe = (f.root / "probe").string();
  // Records the mode of the package's folder and the package's hash.
  c.installer_override = {"/bin/sh", "-c",
                          "stat -c %a \"$(dirname \"$0\")\" > '" + probe + "'; sha256sum \"$0\" >> '" + probe + "'"};
  UpdateService s(c);
  UpdateInstallResult r = install(s, fetch_release(s));
  REQUIRE(r.outcome == UpdateInstallResult::Outcome::Installed);
  const std::string seen = slurp(probe);
  REQUIRE(seen.rfind("700\n", 0) == 0);
  REQUIRE(seen.find(sha256_hex(f.package_bytes)) != std::string::npos);
}
#endif

TEST_CASE("install: a failed installer keeps the verified package for a manual install",
          "[update-service]") {
  Fixture f;

  SECTION("password prompt dismissed (126)") {
    UpdateService s(f.config(126));
    UpdateInstallResult r = install(s, fetch_release(s));
    REQUIRE(r.outcome == UpdateInstallResult::Outcome::Failed);
    REQUIRE(r.message.find("password prompt was dismissed") != std::string::npos);
    REQUIRE_FALSE(r.kept_package.empty());
    REQUIRE(slurp(r.kept_package) == f.package_bytes);
    REQUIRE(r.message.find(r.kept_package) != std::string::npos);
#ifndef __APPLE__
    REQUIRE(r.message.find("sudo apt install " + r.kept_package) != std::string::npos);
#endif
    std::error_code ec;
    fs::remove_all(fs::path(r.kept_package).parent_path(), ec);
  }
  SECTION("not authorised (127)") {
    UpdateService s(f.config(127));
    UpdateInstallResult r = install(s, fetch_release(s));
    REQUIRE(r.outcome == UpdateInstallResult::Outcome::Failed);
    REQUIRE(r.message.find("Not authorised") != std::string::npos);
    std::error_code ec;
    fs::remove_all(fs::path(r.kept_package).parent_path(), ec);
  }
  SECTION("apt fails (100)") {
    UpdateService s(f.config(100));
    UpdateInstallResult r = install(s, fetch_release(s));
    REQUIRE(r.outcome == UpdateInstallResult::Outcome::Failed);
    REQUIRE(r.message.find("exit 100") != std::string::npos);
    std::error_code ec;
    fs::remove_all(fs::path(r.kept_package).parent_path(), ec);
  }
}

TEST_CASE("install: a package that doesn't match the signed sums is never installed",
          "[update-service]") {
  Fixture f;
  f.server.set("/cdn/" + f.package_name, {200, "evil bytes, same name", {}, false});
  UpdateService s(f.config());
  UpdateInstallResult r = install(s, fetch_release(s));
  REQUIRE(r.outcome == UpdateInstallResult::Outcome::Failed);
  REQUIRE(r.message.find("does not match") != std::string::npos);
  REQUIRE(r.kept_package.empty());
  REQUIRE_FALSE(f.installer_ran());
  REQUIRE(entries_in(f.work) == 0);  // the bad download is deleted
}

TEST_CASE("install: a signature by the wrong key stops everything before the package download",
          "[update-service]") {
  Fixture f;
  MinisignTestKey attacker(2);
  f.server.set("/download/SHA256SUMS.minisig", {200, attacker.sign(f.sums), {}, false});
  UpdateService s(f.config());
  UpdateInstallResult r = install(s, fetch_release(s));
  REQUIRE(r.outcome == UpdateInstallResult::Outcome::Failed);
  REQUIRE(r.message.find("signature is not valid") != std::string::npos);
  REQUIRE_FALSE(f.package_requested());
  REQUIRE_FALSE(f.installer_ran());
  REQUIRE(entries_in(f.work) == 0);
}

TEST_CASE("install: SHA256SUMS altered after signing is refused", "[update-service]") {
  Fixture f;
  // An attacker swaps in sums that bless their package; the old signature is kept.
  const std::string evil_sums = std::string(64, 'a') + "  " + f.package_name + "\n";
  f.server.set("/download/SHA256SUMS", {200, evil_sums, {}, false});
  UpdateService s(f.config());
  UpdateInstallResult r = install(s, fetch_release(s));
  REQUIRE(r.outcome == UpdateInstallResult::Outcome::Failed);
  REQUIRE(r.message.find("does not match the file") != std::string::npos);
  REQUIRE_FALSE(f.package_requested());
  REQUIRE_FALSE(f.installer_ran());
}

TEST_CASE("install: garbage in place of a signature is refused", "[update-service]") {
  Fixture f;
  f.server.set("/download/SHA256SUMS.minisig", {200, "<html>nope</html>", {}, false});
  UpdateService s(f.config());
  UpdateInstallResult r = install(s, fetch_release(s));
  REQUIRE(r.outcome == UpdateInstallResult::Outcome::Failed);
  REQUIRE_FALSE(f.package_requested());
  REQUIRE_FALSE(f.installer_ran());
}

TEST_CASE("install: a release without a signature says so and installs nothing", "[update-service]") {
  Fixture f;

  SECTION("no SHA256SUMS.minisig asset") {
    f.publish(/*with_signature_asset=*/false);
    UpdateService s(f.config());
    UpdateInstallResult r = install(s, fetch_release(s));
    REQUIRE(r.outcome == UpdateInstallResult::Outcome::Failed);
    REQUIRE(r.message.find("SHA256SUMS.minisig") != std::string::npos);
    REQUIRE(r.message.find("missing") != std::string::npos);
    REQUIRE(f.server.index_of("/download/SHA256SUMS") < 0);  // not even fetched
    REQUIRE_FALSE(f.package_requested());
  }
  SECTION("the signature asset is listed but gone (404)") {
    f.server.remove("/download/SHA256SUMS.minisig");
    UpdateService s(f.config());
    UpdateInstallResult r = install(s, fetch_release(s));
    REQUIRE(r.outcome == UpdateInstallResult::Outcome::Failed);
    REQUIRE_FALSE(f.package_requested());
  }
  SECTION("no package for this platform") {
    f.publish(true, /*with_package_asset=*/false);
    UpdateService s(f.config());
    UpdateInstallResult r = install(s, fetch_release(s));
    REQUIRE(r.outcome == UpdateInstallResult::Outcome::Failed);
    REQUIRE(r.message.find(f.package_name) != std::string::npos);
  }
  REQUIRE_FALSE(f.installer_ran());
  REQUIRE(entries_in(f.work) == 0);
}

TEST_CASE("install: a release listing path-like asset names is refused",
          "[update-service]") {
  Fixture f;
  UpdateService s(f.config());
  ReleaseInfo release = fetch_release(s);
  for (ReleaseAsset& a : release.assets) {
    if (a.name == "SHA256SUMS") a.name = "../SHA256SUMS";
  }
  // (The asset finders match exact names, so this is refused as "missing";
  // the explicit file-name check in the installer is a second line of defence.)
  UpdateInstallResult r = install(s, release);
  REQUIRE(r.outcome == UpdateInstallResult::Outcome::Failed);
  REQUIRE_FALSE(f.installer_ran());
  REQUIRE_FALSE(fs::exists(f.root / "SHA256SUMS"));
}

TEST_CASE("install: a build without a release key refuses to install", "[update-service]") {
  Fixture f;
  // A key that isn't valid minisign: the installer must refuse, not skip verification.
  set_update_signing_public_key_for_tests("not a key");
  UpdateService s(f.config());
  UpdateInstallResult r = install(s, fetch_release(s));
  REQUIRE(r.outcome == UpdateInstallResult::Outcome::Failed);
  REQUIRE(r.message.find("invalid") != std::string::npos);
  REQUIRE(f.server.index_of("/download/SHA256SUMS") < 0);
  REQUIRE_FALSE(f.installer_ran());
}

TEST_CASE("install: a download can be cancelled", "[update-service]") {
  Fixture f;
  f.server.set("/cdn/" + f.package_name, {200, "", {}, /*endless=*/true});
  UpdateService s(f.config());
  ReleaseInfo release = fetch_release(s);
  s.start_install(release);
  REQUIRE(wait_until([&] { return f.server.count("/cdn/" + f.package_name) > 0; }));
  REQUIRE(wait_until([&] {
    UpdateProgress p = s.progress();
    return p.asset == f.package_name && p.received > 0;
  }));
  s.cancel();
  REQUIRE(wait_until([&] { return !s.busy(); }));
  auto r = s.poll_install();
  REQUIRE(r.has_value());
  REQUIRE(r->outcome == UpdateInstallResult::Outcome::Cancelled);
  REQUIRE_FALSE(f.installer_ran());
  REQUIRE(entries_in(f.work) == 0);
  // Usable again afterwards.
  f.server.set("/cdn/" + f.package_name, {200, f.package_bytes, {}, false});
  REQUIRE(install(s, release).outcome == UpdateInstallResult::Outcome::Installed);
}

TEST_CASE("one job at a time", "[update-service]") {
  Fixture f;
  f.server.set("/cdn/" + f.package_name, {200, "", {}, /*endless=*/true});
  UpdateService s(f.config());
  ReleaseInfo release = fetch_release(s);
  s.start_install(release);
  REQUIRE(s.busy());
  s.start_check(true);    // ignored while an install runs
  s.start_install(release);  // ignored too
  s.cancel();
  REQUIRE(wait_until([&] { return !s.busy(); }));
  REQUIRE_FALSE(s.poll_check().has_value());
  REQUIRE(s.poll_install().has_value());
  REQUIRE_FALSE(s.poll_install().has_value());
}

TEST_CASE("destroying the service mid-download cancels cleanly", "[update-service]") {
  Fixture f;
  f.server.set("/cdn/" + f.package_name, {200, "", {}, /*endless=*/true});
  {
    UpdateService s(f.config());
    s.start_install(fetch_release(s));
    REQUIRE(wait_until([&] {
      UpdateProgress p = s.progress();
      return p.asset == f.package_name && p.received > 0;
    }));
  }  // ~UpdateService
  REQUIRE(entries_in(f.work) == 0);
  REQUIRE_FALSE(f.installer_ran());
}

TEST_CASE("install_command is pkexec apt install -y <absolute path>", "[update-service]") {
  UpdateConfig c;
  auto cmd = UpdateService::install_command(c, "/tmp/x/zedit.deb");
  REQUIRE(cmd == std::vector<std::string>{"/usr/bin/pkexec", "/usr/bin/apt", "install", "-y",
                                          "/tmp/x/zedit.deb"});
  auto relative = UpdateService::install_command(c, "zedit.deb");
  REQUIRE(fs::path(relative.back()).is_absolute());  // so apt reads it as a file, not a package name
  REQUIRE(relative.size() == 5);
}

#ifndef __APPLE__
TEST_CASE("linux: only a packaged /usr/bin/ze install is replaced", "[update-service]") {
  Fixture f;
  const std::string pkexec_record = (f.root / "pkexec-args").string();
  const std::string fake_pkexec = (f.root / "pkexec").string();
  { std::ofstream(fake_pkexec) << "#!/bin/sh\nprintf '%s\\n' \"$@\" > '" << pkexec_record << "'\n"; }
  chmod(fake_pkexec.c_str(), 0755);

  UpdateConfig c = f.config();
  c.installer_override.clear();  // the real code path, with stand-in pkexec/apt
  c.pkexec = fake_pkexec;
  c.apt = "/bin/true";  // only has to exist and be executable: the fake pkexec never runs it

  SECTION("running from somewhere else: not installed, package kept, pkexec never run") {
    c.installed_binary = "/nonexistent/ze";
    UpdateService s(c);
    UpdateInstallResult r = install(s, fetch_release(s));
    REQUIRE(r.outcome == UpdateInstallResult::Outcome::Failed);
    REQUIRE(r.message.find("wasn't installed from the .deb") != std::string::npos);
    REQUIRE_FALSE(r.kept_package.empty());
    REQUIRE(slurp(r.kept_package) == f.package_bytes);
    REQUIRE_FALSE(fs::exists(pkexec_record));
    std::error_code ec;
    fs::remove_all(fs::path(r.kept_package).parent_path(), ec);
  }
  SECTION("running as the installed binary: pkexec apt install -y <staged path>") {
    c.installed_binary = fs::read_symlink("/proc/self/exe").string();
    UpdateService s(c);
    UpdateInstallResult r = install(s, fetch_release(s));
    INFO(r.message);
    REQUIRE(r.outcome == UpdateInstallResult::Outcome::Installed);
    const std::string args = slurp(pkexec_record);
    REQUIRE(args.rfind("/bin/true\ninstall\n-y\n/", 0) == 0);
    REQUIRE(args.find("/" + f.package_name + "\n") != std::string::npos);
    REQUIRE(entries_in(f.work) == 0);
  }
  SECTION("pkexec missing") {
    c.installed_binary = fs::read_symlink("/proc/self/exe").string();
    c.pkexec = "/nonexistent/pkexec";
    UpdateService s(c);
    UpdateInstallResult r = install(s, fetch_release(s));
    REQUIRE(r.outcome == UpdateInstallResult::Outcome::Failed);
    REQUIRE(r.message.find("pkexec or apt isn't available") != std::string::npos);
    std::error_code ec;
    fs::remove_all(fs::path(r.kept_package).parent_path(), ec);
  }
}
#endif

// --- The dialogs' state machine (frontend_imgui/src/update_ui.cpp), headless:
// a real ImGui context with no window or renderer, frames run by hand.
namespace {

struct HeadlessImGui {
  HeadlessImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 800);
    unsigned char* pixels = nullptr;
    int w = 0;
    int h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  }
  ~HeadlessImGui() { ImGui::DestroyContext(); }
  void frame(zedit::frontend::UpdateUi& ui, const zedit::core::Editor& ed) {
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ui.poll();
    ui.render_modals(ed);
    ImGui::Render();
  }
};

bool frames_until(HeadlessImGui& g, zedit::frontend::UpdateUi& ui, const zedit::core::Editor& ed,
                  const std::function<bool()>& done) {
  return wait_until([&] {
    g.frame(ui, ed);
    return done();
  });
}

}  // namespace

TEST_CASE("ui: a manual check offers the update, Install runs it and then offers a restart",
          "[update-service][update-ui]") {
  Fixture f("v9.9.9");  // newer than the running zedit
  HeadlessImGui g;
  zedit::core::Editor ed;
  using Phase = zedit::frontend::UpdateUi::Phase;
  zedit::frontend::UpdateUi ui(f.config(), (f.root / "state").string());
  REQUIRE(ui.phase() == Phase::None);

  ui.check_now();
  REQUIRE(frames_until(g, ui, ed, [&] { return ui.phase() == Phase::Offer; }));
  REQUIRE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));

  ui.accept_offer();
  REQUIRE(ui.phase() == Phase::Progress);
#ifdef __APPLE__
  REQUIRE(frames_until(g, ui, ed, [&] { return ui.phase() != Phase::Progress; }));
  REQUIRE(ui.phase() == Phase::Message);  // the macOS install ends in a message, no restart
#else
  REQUIRE(frames_until(g, ui, ed, [&] { return ui.phase() == Phase::Restart; }));
  REQUIRE(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
  REQUIRE_FALSE(ui.restart_requested());  // only the button asks for it
#endif
  REQUIRE(f.installer_ran());
}

TEST_CASE("ui: a failed install shows a message, not a restart", "[update-service][update-ui]") {
  Fixture f("v9.9.9");
  f.server.set("/cdn/" + f.package_name, {200, "tampered", {}, false});
  HeadlessImGui g;
  zedit::core::Editor ed;
  using Phase = zedit::frontend::UpdateUi::Phase;
  zedit::frontend::UpdateUi ui(f.config(), (f.root / "state").string());
  ui.check_now();
  REQUIRE(frames_until(g, ui, ed, [&] { return ui.phase() == Phase::Offer; }));
  ui.accept_offer();
  REQUIRE(frames_until(g, ui, ed, [&] { return ui.phase() == Phase::Message; }));
  REQUIRE_FALSE(f.installer_ran());
}

TEST_CASE("ui: up to date and errors are reported; the startup check is quiet",
          "[update-service][update-ui]") {
  using Phase = zedit::frontend::UpdateUi::Phase;
  HeadlessImGui g;
  zedit::core::Editor ed;
  SECTION("manual check with nothing newer") {
    Fixture f("v0.0.1");
    zedit::frontend::UpdateUi ui(f.config(), (f.root / "state").string());
    ui.check_now();
    REQUIRE(frames_until(g, ui, ed, [&] { return ui.phase() != Phase::None; }));
    REQUIRE(ui.phase() == Phase::Message);
  }
  SECTION("manual check that fails") {
    Fixture f("v9.9.9");
    f.server.set("/latest", {500, "", {}, false});
    zedit::frontend::UpdateUi ui(f.config(), (f.root / "state").string());
    ui.check_now();
    REQUIRE(frames_until(g, ui, ed, [&] { return ui.phase() != Phase::None; }));
    REQUIRE(ui.phase() == Phase::Message);
  }
  SECTION("the quiet startup check offers only when no other dialog is open, and once a day") {
    Fixture f("v9.9.9");
    zedit::frontend::UpdateUi ui(f.config(), (f.root / "state").string());
    ui.start_startup_check();
    bool other_wanted = true;
    bool close_other = false;
    auto frame = [&] {
      ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
      ImGui::NewFrame();
      ui.poll();
      if (other_wanted) {  // some other dialog the user is in the middle of
        if (!ImGui::IsPopupOpen("Other")) ImGui::OpenPopup("Other");
        if (ImGui::BeginPopupModal("Other")) {
          if (close_other) {
            ImGui::CloseCurrentPopup();
            other_wanted = false;
          }
          ImGui::EndPopup();
        }
      }
      ui.render_modals(ed);
      ImGui::Render();
    };
    REQUIRE(wait_until([&] {
      frame();
      return f.server.count("/latest") > 0;
    }));
    for (int i = 0; i < 40; ++i) {  // the result has arrived; it must not barge in
      frame();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(ui.phase() == Phase::None);
    close_other = true;
    REQUIRE(wait_until([&] {
      frame();
      return ui.phase() == Phase::Offer;
    }));
    REQUIRE_FALSE(f.installer_ran());
    // The check is remembered: a second launch within a day doesn't ask again.
    zedit::frontend::UpdateUi again(f.config(), (f.root / "state").string());
    again.start_startup_check();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    REQUIRE(f.server.count("/latest") == 1);
  }
}
