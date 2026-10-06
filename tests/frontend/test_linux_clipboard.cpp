// Linux only: the clipboard readers behind read_linux_clipboard_text()
// (frontend_imgui/src/linux_clipboard.cpp), the clipboard bridge's read
// callback on Linux.
//
// "[wl-paste]" cases drive WlPasteClipboard against fake wl-paste scripts
// (no display needed) and always run.
//
// "[real-clipboard]" cases put data on the real X11 CLIPBOARD ("[x11]",
// via xclip; needs $DISPLAY, e.g. Xvfb) or the real Wayland clipboard
// ("[wayland]", via tests/helpers/wl_clipboard_owner.cpp; needs a
// wlr-data-control compositor such as headless sway, and wl-paste) and
// paste it through the editor exactly as App does. They overwrite the
// clipboard, so they only run when ZEDIT_TEST_REAL_CLIPBOARD=1 and the
// display they need is there; otherwise they are skipped. CI sets it.

#include <catch2/catch_test_macros.hpp>

#include <GLFW/glfw3.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "linux_clipboard.hpp"
#include "wl_paste_clipboard.hpp"
#include "x11_clipboard.hpp"
#include "x11_test_owner.hpp"
#include "zedit/core/clipboard_source.hpp"
#include "zedit/core/editor.hpp"

extern char** environ;

using zedit::core::Editor;
using zedit::core::Key;
using zedit::core::KeyEvent;
using zedit::core::mime_clipboard_plain_text;
using zedit::core::MimeClipboardSource;
using zedit::frontend::note_linux_clipboard_write;
using zedit::frontend::read_linux_clipboard_text;
using zedit::frontend::WlPasteClipboard;
using zedit::frontend::X11Clipboard;

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

constexpr std::string_view kChatHtml =
    "<meta charset='utf-8'><p>To fix the build:</p>"
    "<ol><li>Run <code>cmake -B build</code></li>"
    "<li>Then run <code>ctest --test-dir build</code></li></ol>";
constexpr std::string_view kChatText =
    "To fix the build:\n\n1. Run cmake -B build\n2. Then run ctest --test-dir build";
constexpr std::string_view kRtf =
    "{\\rtf1\\ansi{\\fonttbl\\f0 Helvetica;}\\f0 Caf\\'e9\\par second line}";
constexpr std::string_view kRtfText = "Caf\xC3\xA9\nsecond line";

long elapsed_ms(Clock::time_point since) {
  return static_cast<long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count());
}

bool env_is(const char* name, std::string_view value) {
  const char* v = std::getenv(name);
  return v != nullptr && std::string_view(v) == value;
}
bool env_set(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && *v != '\0';
}

struct TempDir {
  fs::path path;
  TempDir() {
    std::string tmpl = (fs::temp_directory_path() / "zedit-clip-XXXXXX").string();
    path = mkdtemp(tmpl.data());
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  fs::path write(const std::string& name, std::string_view content, bool executable = false) {
    fs::path p = path / name;
    std::ofstream(p, std::ios::binary) << content;
    if (executable) chmod(p.c_str(), 0755);
    return p;
  }
};

// A child process (xclip, the Wayland owner helper) killed on scope exit.
struct Child {
  pid_t pid = -1;
  int out_fd = -1;

  // Runs argv with stdin from `stdin_file` (or /dev/null) and stdout piped
  // to out_fd; stderr is inherited so failures show in the test log.
  Child(const std::vector<std::string>& args, const std::string& stdin_file = "/dev/null") {
    int out[2];
    if (pipe2(out, O_CLOEXEC) != 0) return;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, stdin_file.c_str(), O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    std::vector<std::string> storage = args;
    std::vector<char*> argv;
    for (std::string& a : storage) argv.push_back(a.data());
    argv.push_back(nullptr);
    if (posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&actions);
    close(out[1]);
    out_fd = out[0];
  }
  ~Child() {
    if (pid > 0) {
      kill(pid, SIGTERM);
      waitpid(pid, nullptr, 0);
    }
    if (out_fd >= 0) close(out_fd);
  }
  Child(const Child&) = delete;
  Child& operator=(const Child&) = delete;

  // Waits up to timeout_ms for `line` on the child's stdout.
  bool wait_for_line(std::string_view line, int timeout_ms) {
    std::string got;
    auto start = Clock::now();
    while (elapsed_ms(start) < timeout_ms) {
      pollfd pfd{out_fd, POLLIN, 0};
      if (poll(&pfd, 1, 100) <= 0) continue;
      char c;
      if (read(out_fd, &c, 1) != 1) return false;
      if (c == '\n') {
        if (got == line) return true;
        got.clear();
      } else {
        got += c;
      }
    }
    return false;
  }
};

// glfwInit() on the requested platform plus a hidden window, as main.cpp
// has when App reads the clipboard.
struct GlfwSession {
  GLFWwindow* window = nullptr;
  explicit GlfwSession(int platform) {
    glfwInitHint(GLFW_PLATFORM, platform);
    if (!glfwInit()) return;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(64, 64, "zedit clipboard test", nullptr, nullptr);
  }
  ~GlfwSession() {
    if (window != nullptr) glfwDestroyWindow(window);
    glfwTerminate();
  }
  GlfwSession(const GlfwSession&) = delete;
  GlfwSession& operator=(const GlfwSession&) = delete;
};

// The Editor with its clipboard bridge wired exactly as App does on Linux.
void attach_like_app(Editor& ed) {
  ed.set_clipboard_bridge(Editor::ClipboardBridge{
      [] { return read_linux_clipboard_text(); },
      [](const std::string& text) {
        glfwSetClipboardString(nullptr, text.c_str());
        note_linux_clipboard_write();
      },
  });
}

// Insert-mode Ctrl-P -- what Ctrl-Shift-V / Edit > Paste send.
std::string paste_like_app() {
  Editor ed;
  attach_like_app(ed);
  REQUIRE(ed.can_paste());  // Edit > Paste is enabled
  ed.handle_key(KeyEvent{Key::Char, 'i'});
  ed.handle_key(KeyEvent{Key::CtrlP, 0});
  return ed.buffer().to_string();
}

bool contains(const std::vector<std::string>& v, std::string_view s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

void require_real_x11() {
  if (!env_is("ZEDIT_TEST_REAL_CLIPBOARD", "1")) {
    SKIP("set ZEDIT_TEST_REAL_CLIPBOARD=1 to use the real X11 clipboard (overwrites it)");
  }
  if (!env_set("DISPLAY")) SKIP("no $DISPLAY (run under Xvfb / xvfb-run)");
}

void require_real_wayland() {
  if (!env_is("ZEDIT_TEST_REAL_CLIPBOARD", "1")) {
    SKIP("set ZEDIT_TEST_REAL_CLIPBOARD=1 to use the real Wayland clipboard (overwrites it)");
  }
  if (!env_set("WAYLAND_DISPLAY")) SKIP("no $WAYLAND_DISPLAY (run under e.g. headless sway)");
}

// Puts exactly one target on the real X11 CLIPBOARD via xclip (which offers
// just TARGETS and that one type -- no UTF8_STRING) and waits until it's
// there.
std::unique_ptr<Child> xclip_offer(X11Clipboard& x11, const TempDir& dir, const fs::path& file,
                                   const std::string& type) {
  (void)dir;
  auto child = std::make_unique<Child>(
      std::vector<std::string>{"xclip", "-quiet", "-selection", "clipboard", "-t", type, "-i"},
      file.string());
  REQUIRE(child->pid > 0);
  auto start = Clock::now();
  while (elapsed_ms(start) < 5000) {
    std::optional<std::vector<std::string>> targets = x11.targets();
    if (targets && contains(*targets, type)) return child;
    usleep(50 * 1000);
  }
  FAIL("xclip never took the CLIPBOARD selection");
  return child;
}

#ifdef ZEDIT_WL_OWNER_HELPER
std::unique_ptr<Child> wayland_offer(const std::vector<std::string>& type_files, bool hang = false) {
  std::vector<std::string> args{ZEDIT_WL_OWNER_HELPER};
  if (hang) args.emplace_back("--hang");
  args.insert(args.end(), type_files.begin(), type_files.end());
  auto child = std::make_unique<Child>(args);
  REQUIRE(child->pid > 0);
  REQUIRE(child->wait_for_line("ready", 5000));
  return child;
}
#endif

}  // namespace

// ---------------------------------------------------------------------------
// WlPasteClipboard against fake wl-paste programs (always run)
// ---------------------------------------------------------------------------

TEST_CASE("wl-paste: HTML-only offer is listed, read and converted", "[clipboard][wl-paste]") {
  TempDir dir;
  dir.write("page.html", kChatHtml);
  fs::path fake = dir.write("wl-paste",
                            "#!/bin/sh\n"
                            "if [ \"$1\" = --list-types ]; then printf 'text/html\\n'; exit 0; fi\n"
                            "if [ \"$1 $2 $3\" = '--no-newline --type text/html' ]; then\n"
                            "  cat \"$(dirname \"$0\")/page.html\"; exit 0; fi\n"
                            "echo 'No suitable type of content copied' >&2; exit 1\n",
                            true);
  WlPasteClipboard wl({fake.string(), 2000, 64u << 20});
  REQUIRE(wl.targets() == std::vector<std::string>{"text/html"});
  REQUIRE(mime_clipboard_plain_text(wl) == std::string(kChatText));
  REQUIRE_FALSE(wl.program_missing());
}

TEST_CASE("wl-paste: empty clipboard (wl-paste exits 1) offers nothing", "[clipboard][wl-paste]") {
  TempDir dir;
  fs::path fake = dir.write("wl-paste", "#!/bin/sh\necho 'Nothing is copied' >&2\nexit 1\n", true);
  WlPasteClipboard wl({fake.string(), 2000, 64u << 20});
  REQUIRE(wl.targets() == std::nullopt);
  REQUIRE(mime_clipboard_plain_text(wl) == std::nullopt);
}

TEST_CASE("wl-paste: an owner that never sends its data times out", "[clipboard][wl-paste]") {
  TempDir dir;
  fs::path fake = dir.write("wl-paste",
                            "#!/bin/sh\n"
                            "if [ \"$1\" = --list-types ]; then printf 'text/html\\ntext/rtf\\n'; exit 0; fi\n"
                            "exec sleep 30\n",
                            true);
  WlPasteClipboard wl({fake.string(), 300, 64u << 20});
  auto start = Clock::now();
  REQUIRE(wl.read("text/html").status == MimeClipboardSource::ReadStatus::timed_out);
  long one_read = elapsed_ms(start);
  CHECK(one_read >= 250);
  CHECK(one_read < 2000);
  // The whole paste gives up after that one timeout (text/rtf isn't tried).
  start = Clock::now();
  REQUIRE(mime_clipboard_plain_text(wl) == std::nullopt);
  CHECK(elapsed_ms(start) < 2000);
}

TEST_CASE("wl-paste: missing wl-clipboard degrades to nothing", "[clipboard][wl-paste]") {
  WlPasteClipboard wl({"zedit-test-no-such-wl-paste", 1000, 64u << 20});
  REQUIRE(wl.targets() == std::nullopt);
  REQUIRE(wl.program_missing());
  REQUIRE(mime_clipboard_plain_text(wl) == std::nullopt);
}

// ---------------------------------------------------------------------------
// Real X11 CLIPBOARD (ZEDIT_TEST_REAL_CLIPBOARD=1 + $DISPLAY)
// ---------------------------------------------------------------------------

TEST_CASE("X11: HTML-only data on the real CLIPBOARD pastes into the editor",
          "[clipboard][linux][real-clipboard][x11]") {
  require_real_x11();
  GlfwSession glfw(GLFW_PLATFORM_X11);
  REQUIRE(glfw.window != nullptr);
  REQUIRE(zedit::frontend::linux_clipboard_backend() ==
          zedit::frontend::LinuxClipboardBackend::x11);
  std::unique_ptr<X11Clipboard> x11 = X11Clipboard::open();
  REQUIRE(x11 != nullptr);

  TempDir dir;
  auto owner = xclip_offer(*x11, dir, dir.write("chat.html", kChatHtml), "text/html");
  std::optional<std::vector<std::string>> targets = x11->targets();
  REQUIRE(targets.has_value());
  INFO("targets offered by xclip:");
  for (const std::string& t : *targets) UNSCOPED_INFO(t);
  // The premise of the bug: no plain-text target, so GLFW's read (all that
  // zedit 1.7.3 and earlier used on Linux) gets nothing.
  for (std::string_view plain : {"UTF8_STRING", "STRING", "text/plain", "text/plain;charset=utf-8"}) {
    REQUIRE_FALSE(contains(*targets, plain));
  }
  REQUIRE(glfwGetClipboardString(nullptr) == nullptr);
  // xclip answers a UTF8_STRING request anyway, with its HTML typed
  // text/html; that must not be taken for plain text (raw markup).
  REQUIRE(x11->read("UTF8_STRING").status == MimeClipboardSource::ReadStatus::unavailable);

  REQUIRE(paste_like_app() == kChatText);
}

TEST_CASE("X11: RTF-only data on the real CLIPBOARD pastes into the editor",
          "[clipboard][linux][real-clipboard][x11]") {
  require_real_x11();
  GlfwSession glfw(GLFW_PLATFORM_X11);
  REQUIRE(glfw.window != nullptr);
  std::unique_ptr<X11Clipboard> x11 = X11Clipboard::open();
  REQUIRE(x11 != nullptr);
  TempDir dir;
  auto owner = xclip_offer(*x11, dir, dir.write("note.rtf", kRtf), "text/rtf");
  REQUIRE(glfwGetClipboardString(nullptr) == nullptr);
  REQUIRE(paste_like_app() == kRtfText);
}

TEST_CASE("X11: a large HTML-only copy arrives via INCR", "[clipboard][linux][real-clipboard][x11]") {
  require_real_x11();
  GlfwSession glfw(GLFW_PLATFORM_X11);
  REQUIRE(glfw.window != nullptr);
  std::unique_ptr<X11Clipboard> x11 = X11Clipboard::open();
  REQUIRE(x11 != nullptr);

  // ~6 MB: well past what fits in one X request, so xclip has to use INCR.
  std::string html = "<ol>";
  std::string expected;
  for (int i = 1; i <= 120000; ++i) {
    std::string item = "item <code>" + std::to_string(i) + "</code> of a long list";
    html += "<li>" + item + "</li>";
    if (i > 1) expected += "\n";
    expected += std::to_string(i) + ". item " + std::to_string(i) + " of a long list";
  }
  html += "</ol>";
  REQUIRE(html.size() > 4'000'000);
  TempDir dir;
  auto owner = xclip_offer(*x11, dir, dir.write("big.html", html), "text/html");

  X11Clipboard::Read r = x11->read("text/html");
  REQUIRE(r.status == MimeClipboardSource::ReadStatus::ok);
  REQUIRE(r.data.size() == html.size());
  REQUIRE(x11->last_read_used_incr());
  REQUIRE(r.data == html);

  auto start = Clock::now();
  std::optional<std::string> text = read_linux_clipboard_text();
  INFO("read + convert took " << elapsed_ms(start) << " ms");
  REQUIRE(text.has_value());
  REQUIRE(text->size() == expected.size());
  REQUIRE(*text == expected);
}

TEST_CASE("X11: zedit's own copy pastes back without a round trip to itself",
          "[clipboard][linux][real-clipboard][x11]") {
  require_real_x11();
  GlfwSession glfw(GLFW_PLATFORM_X11);
  REQUIRE(glfw.window != nullptr);

  Editor ed;
  attach_like_app(ed);
  for (char c : std::string_view("iown copy")) ed.handle_key(KeyEvent{Key::Char, c});
  ed.handle_key(KeyEvent{Key::Escape, 0});
  for (char c : std::string_view("0yiw")) ed.handle_key(KeyEvent{Key::Char, c});  // yank "own"

  // GLFW owns CLIPBOARD now and only answers requests from its event loop,
  // which isn't running during a paste; asking it over the wire would just
  // time out. read_linux_clipboard_text() recognises the owner instead.
  auto start = Clock::now();
  REQUIRE(read_linux_clipboard_text() == std::string("own"));
  CHECK(elapsed_ms(start) < 500);
}

TEST_CASE("X11: an owner that never answers costs one timeout, then paste uses the register",
          "[clipboard][linux][real-clipboard][x11]") {
  require_real_x11();
  GlfwSession glfw(GLFW_PLATFORM_X11);
  REQUIRE(glfw.window != nullptr);

  // A client that takes CLIPBOARD and then never processes its events.
  zedit::test::HungX11ClipboardOwner hung;
  REQUIRE(hung.owns_clipboard());

  auto start = Clock::now();
  REQUIRE(read_linux_clipboard_text() == std::nullopt);
  long first = elapsed_ms(start);
  INFO("first read gave up after " << first << " ms");
  CHECK(first >= 900);
  CHECK(first < 3000);

  // Not asked again while it still owns the clipboard.
  start = Clock::now();
  REQUIRE(read_linux_clipboard_text() == std::nullopt);
  CHECK(elapsed_ms(start) < 200);

  // Paste falls back to zedit's own register instead of hanging.
  Editor ed;
  ed.set_clipboard_bridge(Editor::ClipboardBridge{
      [] { return read_linux_clipboard_text(); }, [](const std::string&) {}});
  for (char c : std::string_view("iab")) ed.handle_key(KeyEvent{Key::Char, c});
  ed.handle_key(KeyEvent{Key::Escape, 0});
  for (char c : std::string_view("0yl$p")) ed.handle_key(KeyEvent{Key::Char, c});
  REQUIRE(ed.buffer().to_string() == "aba");
}

// ---------------------------------------------------------------------------
// Real Wayland clipboard (ZEDIT_TEST_REAL_CLIPBOARD=1 + $WAYLAND_DISPLAY)
// ---------------------------------------------------------------------------

TEST_CASE("Wayland: HTML-only data on the real clipboard pastes into the editor",
          "[clipboard][linux][real-clipboard][wayland]") {
  require_real_wayland();
#ifndef ZEDIT_WL_OWNER_HELPER
  FAIL("built without the Wayland owner helper (needs wayland-client + wayland-scanner)");
#else
  REQUIRE(zedit::frontend::program_on_path("wl-paste"));
  GlfwSession glfw(GLFW_PLATFORM_WAYLAND);
  REQUIRE(glfw.window != nullptr);
  REQUIRE(zedit::frontend::linux_clipboard_backend() ==
          zedit::frontend::LinuxClipboardBackend::wayland);
  TempDir dir;
  auto owner = wayland_offer({"text/html=" + dir.write("chat.html", kChatHtml).string()});
  // What the compositor offers: text/html and nothing else.
  WlPasteClipboard wl;
  REQUIRE(wl.targets() == std::vector<std::string>{"text/html"});
  REQUIRE(paste_like_app() == kChatText);
#endif
}

TEST_CASE("Wayland: RTF-only data on the real clipboard pastes into the editor",
          "[clipboard][linux][real-clipboard][wayland]") {
  require_real_wayland();
#ifndef ZEDIT_WL_OWNER_HELPER
  FAIL("built without the Wayland owner helper");
#else
  GlfwSession glfw(GLFW_PLATFORM_WAYLAND);
  REQUIRE(glfw.window != nullptr);
  TempDir dir;
  auto owner = wayland_offer({"text/rtf=" + dir.write("note.rtf", kRtf).string()});
  REQUIRE(paste_like_app() == kRtfText);
#endif
}

TEST_CASE("Wayland: plain text wins over HTML on the real clipboard",
          "[clipboard][linux][real-clipboard][wayland]") {
  require_real_wayland();
#ifndef ZEDIT_WL_OWNER_HELPER
  FAIL("built without the Wayland owner helper");
#else
  GlfwSession glfw(GLFW_PLATFORM_WAYLAND);
  REQUIRE(glfw.window != nullptr);
  TempDir dir;
  auto owner = wayland_offer({"text/html=" + dir.write("a.html", "<b>html loses</b>").string(),
                              "text/plain;charset=utf-8=" + dir.write("a.txt", "plain wins").string()});
  REQUIRE(paste_like_app() == "plain wins");
#endif
}

TEST_CASE("Wayland: an owner that never sends its data doesn't hang the paste",
          "[clipboard][linux][real-clipboard][wayland]") {
  require_real_wayland();
#ifndef ZEDIT_WL_OWNER_HELPER
  FAIL("built without the Wayland owner helper");
#else
  GlfwSession glfw(GLFW_PLATFORM_WAYLAND);
  REQUIRE(glfw.window != nullptr);
  TempDir dir;
  auto owner = wayland_offer({"text/html=" + dir.write("chat.html", kChatHtml).string()},
                             /*hang=*/true);
  auto start = Clock::now();
  REQUIRE(read_linux_clipboard_text() == std::nullopt);
  long took = elapsed_ms(start);
  INFO("gave up after " << took << " ms");
  CHECK(took < 3000);
#endif
}
