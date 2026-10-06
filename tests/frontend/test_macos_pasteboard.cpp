// macOS only: puts HTML-only / RTF-only data on a real NSPasteboard and
// reads it back through the same reader the app's clipboard bridge uses.
//
// By default these use a private, uniquely named pasteboard so running the
// suite never clobbers the user's clipboard. The "[real-clipboard]" case
// uses the general pasteboard (the one Cmd-C/Cmd-V use) and only runs when
// ZEDIT_TEST_REAL_CLIPBOARD=1 (CI sets it); otherwise it is skipped.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "macos_pasteboard.hpp"
#include "zedit/core/clipboard_text.hpp"
#include "zedit/core/editor.hpp"

using zedit::core::clipboard_plain_text;
using zedit::core::ClipboardFlavors;
using zedit::core::Editor;
using zedit::core::Key;
using zedit::core::KeyEvent;
using zedit::frontend::create_unique_macos_pasteboard;
using zedit::frontend::macos_pasteboard_has_type;
using zedit::frontend::read_macos_clipboard_text;
using zedit::frontend::read_macos_pasteboard;
using zedit::frontend::release_macos_pasteboard;
using zedit::frontend::write_macos_pasteboard;

namespace {

constexpr std::string_view kChatHtml =
    "<meta charset='utf-8'><p>To fix the build:</p>"
    "<ol><li>Run <code>cmake -B build</code></li>"
    "<li>Then run <code>ctest --test-dir build</code></li></ol>";
constexpr std::string_view kChatText =
    "To fix the build:\n\n1. Run cmake -B build\n2. Then run ctest --test-dir build";

constexpr std::string_view kPlainUti = "public.utf8-plain-text";

struct PrivatePasteboard {
  std::string name = create_unique_macos_pasteboard();
  ~PrivatePasteboard() { release_macos_pasteboard(name); }
};

void type_keys(Editor& ed, std::string_view keys) {
  for (char c : keys) ed.handle_key(KeyEvent{Key::Char, c});
}

}  // namespace

TEST_CASE("macOS: HTML-only pasteboard reads back as plain text", "[clipboard][macos]") {
  PrivatePasteboard pb;
  REQUIRE_FALSE(pb.name.empty());
  ClipboardFlavors html_only;
  html_only.html = std::string(kChatHtml);
  write_macos_pasteboard(pb.name, html_only);

  // The premise of the bug: no plain-text flavor, which is all GLFW reads.
  REQUIRE(macos_pasteboard_has_type(pb.name, "public.html"));
  REQUIRE_FALSE(macos_pasteboard_has_type(pb.name, std::string(kPlainUti)));

  ClipboardFlavors read = read_macos_pasteboard(pb.name);
  REQUIRE_FALSE(read.plain_text.has_value());
  REQUIRE(read.html.has_value());
  REQUIRE(clipboard_plain_text(read) == std::string(kChatText));
}

TEST_CASE("macOS: RTF-only pasteboard reads back as plain text", "[clipboard][macos]") {
  PrivatePasteboard pb;
  ClipboardFlavors rtf_only;
  rtf_only.rtf = "{\\rtf1\\ansi{\\fonttbl\\f0 Helvetica;}\\f0 Caf\\'e9\\par second line}";
  write_macos_pasteboard(pb.name, rtf_only);
  REQUIRE(macos_pasteboard_has_type(pb.name, "public.rtf"));
  REQUIRE_FALSE(macos_pasteboard_has_type(pb.name, std::string(kPlainUti)));

  ClipboardFlavors read = read_macos_pasteboard(pb.name);
  REQUIRE(read.rtf.has_value());
  REQUIRE(clipboard_plain_text(read) == std::string("Caf\xC3\xA9\nsecond line"));
}

TEST_CASE("macOS: plain text wins when the pasteboard also has HTML", "[clipboard][macos]") {
  PrivatePasteboard pb;
  ClipboardFlavors both;
  both.plain_text = "plain wins";
  both.html = "<b>html loses</b>";
  write_macos_pasteboard(pb.name, both);
  REQUIRE(clipboard_plain_text(read_macos_pasteboard(pb.name)) == std::string("plain wins"));
}

TEST_CASE("macOS: an empty pasteboard offers nothing", "[clipboard][macos]") {
  PrivatePasteboard pb;
  write_macos_pasteboard(pb.name, ClipboardFlavors{});
  REQUIRE(clipboard_plain_text(read_macos_pasteboard(pb.name)) == std::nullopt);
}

TEST_CASE("macOS: HTML-only data on the real system clipboard pastes into the editor",
          "[clipboard][macos][real-clipboard]") {
  const char* opt_in = std::getenv("ZEDIT_TEST_REAL_CLIPBOARD");
  if (opt_in == nullptr || std::string_view(opt_in) != "1") {
    SKIP("set ZEDIT_TEST_REAL_CLIPBOARD=1 to use the general pasteboard (overwrites it)");
  }

  ClipboardFlavors html_only;
  html_only.html = std::string(kChatHtml);
  write_macos_pasteboard(/*general pasteboard*/ {}, html_only);
  REQUIRE(macos_pasteboard_has_type({}, "public.html"));
  REQUIRE_FALSE(macos_pasteboard_has_type({}, std::string(kPlainUti)));

  // Wired exactly like App does on macOS.
  Editor ed;
  ed.set_clipboard_bridge(Editor::ClipboardBridge{
      [] { return read_macos_clipboard_text(); },
      [](const std::string&) {},
  });
  REQUIRE(ed.can_paste());  // Edit > Paste is enabled
  type_keys(ed, "i");
  ed.handle_key(KeyEvent{Key::CtrlP, 0});  // Cmd-V / Edit > Paste
  REQUIRE(ed.buffer().to_string() == kChatText);

  // A later copy is picked up (the changeCount cache invalidates).
  ClipboardFlavors rtf_only;
  rtf_only.rtf = "{\\rtf1\\ansi second copy}";
  write_macos_pasteboard({}, rtf_only);
  REQUIRE(read_macos_clipboard_text() == std::string("second copy"));
}
