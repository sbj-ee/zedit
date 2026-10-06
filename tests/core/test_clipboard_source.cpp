// How zedit reads a clipboard that is fetched one MIME type / X11 target at
// a time (X11 CLIPBOARD, Wayland data offers): which types it asks for, in
// what order, how it decodes them, and what it does when the owner never
// answers. A fake source stands in for the real X11/Wayland clipboard; the
// real ones are exercised by tests/frontend/test_linux_clipboard.cpp.

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "zedit/core/clipboard_source.hpp"
#include "zedit/core/clipboard_text.hpp"
#include "zedit/core/editor.hpp"

using zedit::core::decode_clipboard_text;
using zedit::core::Editor;
using zedit::core::Key;
using zedit::core::KeyEvent;
using zedit::core::mime_clipboard_plain_text;
using zedit::core::MimeClipboardSource;
using zedit::core::read_mime_clipboard;

namespace {

// Same Electron chat-app selection as tests/core/test_clipboard_text.cpp: a
// paragraph, a numbered list with inline code, and a closing line.
constexpr std::string_view kChatHtml =
    "<meta charset='utf-8'><div class=\"message\"><p>To fix the build:</p>"
    "<ol><li>Run <code>cmake -B build</code></li>"
    "<li>Then run <code>ctest --test-dir build</code> &amp; check the output</li></ol>"
    "<p>Done&nbsp;&mdash; thanks!</p></div>";
constexpr std::string_view kChatText =
    "To fix the build:\n"
    "\n"
    "1. Run cmake -B build\n"
    "2. Then run ctest --test-dir build & check the output\n"
    "\n"
    "Done \xE2\x80\x94 thanks!";

constexpr std::string_view kRtf =
    "{\\rtf1\\ansi\\ansicpg1252{\\fonttbl\\f0\\fswiss Helvetica;}{\\colortbl;\\red0\\green0\\blue0;}"
    "\\f0\\fs24 {\\listtext\t1.\t}Run make test\\par {\\listtext\t2.\t}Caf\\'e9 \\u8212? done}";
constexpr std::string_view kRtfText = "1. Run make test\n2. Caf\xC3\xA9 \xE2\x80\x94 done";

// A clipboard owner: the types it offers and what it answers for each.
// `hang` makes it never answer (an X11 owner that never sends
// SelectionNotify, a Wayland source that never writes to the pipe); the
// real sources turn that into ReadStatus::timed_out after their timeout.
struct FakeClipboard final : MimeClipboardSource {
  bool has_owner = true;
  bool refuses_targets = false;  // X11 owner that doesn't implement TARGETS
  bool hang_on_targets = false;
  std::vector<std::string> hang_on;           // types whose read never completes
  std::vector<std::pair<std::string, std::string>> offers;  // type -> bytes, in offer order
  std::vector<std::string> requests;          // every type asked for, in order

  std::optional<std::vector<std::string>> targets() override {
    requests.emplace_back("TARGETS");
    if (!has_owner || hang_on_targets) return std::nullopt;
    std::vector<std::string> names;
    if (refuses_targets) return names;
    names.emplace_back("TARGETS");
    for (const auto& offer : offers) names.push_back(offer.first);
    return names;
  }

  Read read(const std::string& type) override {
    requests.push_back(type);
    Read r;
    for (const std::string& h : hang_on) {
      if (h == type) {
        r.status = ReadStatus::timed_out;
        return r;
      }
    }
    for (const auto& offer : offers) {
      if (offer.first == type) {
        r.status = ReadStatus::ok;
        r.data = offer.second;
      }
    }
    return r;
  }

  FakeClipboard& offer(std::string type, std::string_view bytes) {
    offers.emplace_back(std::move(type), std::string(bytes));
    return *this;
  }
};

// Wires the fake clipboard into an Editor the way App wires the real one
// (frontend_imgui/src/app.cpp -> read_linux_clipboard_text()).
void attach(Editor& ed, FakeClipboard& clipboard) {
  ed.set_clipboard_bridge(Editor::ClipboardBridge{
      [&clipboard] { return mime_clipboard_plain_text(clipboard); },
      [](const std::string&) {},
  });
}

void feed(Editor& ed, std::string_view keys) {
  for (char c : keys) {
    KeyEvent ev;
    if (c == '\x1b') {
      ev.key = Key::Escape;
    } else {
      ev.key = Key::Char;
      ev.ch = c;
    }
    ed.handle_key(ev);
  }
}

// Insert-mode Ctrl-P (what Ctrl-Shift-V / Edit > Paste send) into an empty
// buffer.
std::string paste_into_empty_buffer(FakeClipboard& clipboard) {
  Editor ed;
  attach(ed, clipboard);
  feed(ed, "i");
  ed.handle_key(KeyEvent{Key::CtrlP, 0});
  return ed.buffer().to_string();
}

}  // namespace

// ---------------------------------------------------------------------------
// The clipboard shapes from the bug report, as X11/Wayland present them
// ---------------------------------------------------------------------------

TEST_CASE("Linux: HTML-only clipboard (numbered list + inline code) pastes as plain text",
          "[clipboard][linux-select]") {
  // What Chromium/Electron offered on X11 for the selection: no
  // UTF8_STRING / text/plain at all, so glfwGetClipboardString() got NULL.
  FakeClipboard clipboard;
  clipboard.offer("text/html", kChatHtml);
  REQUIRE(mime_clipboard_plain_text(clipboard) == std::string(kChatText));
  // Asked what's offered, then fetched only the HTML.
  REQUIRE(clipboard.requests == std::vector<std::string>{"TARGETS", "text/html"});

  Editor ed;
  attach(ed, clipboard);
  REQUIRE(ed.can_paste());  // Edit > Paste enabled
  REQUIRE(paste_into_empty_buffer(clipboard) == kChatText);
}

TEST_CASE("Linux: RTF-only clipboard pastes as plain text", "[clipboard][linux-select]") {
  for (const char* type : {"text/rtf", "application/rtf", "text/richtext"}) {
    INFO(type);
    FakeClipboard clipboard;
    clipboard.offer(type, kRtf);
    REQUIRE(mime_clipboard_plain_text(clipboard) == std::string(kRtfText));
    REQUIRE(paste_into_empty_buffer(clipboard) == kRtfText);
  }
}

TEST_CASE("Linux: plain text wins over HTML, and the HTML isn't even fetched",
          "[clipboard][linux-select]") {
  FakeClipboard clipboard;
  clipboard.offer("text/html", "<b>html loses</b>")
      .offer("text/rtf", "{\\rtf1 rtf loses}")
      .offer("UTF8_STRING", "plain wins")
      .offer("text/plain;charset=utf-8", "plain wins");
  REQUIRE(mime_clipboard_plain_text(clipboard) == std::string("plain wins"));
  REQUIRE(clipboard.requests == std::vector<std::string>{"TARGETS", "UTF8_STRING"});
  REQUIRE(paste_into_empty_buffer(clipboard) == "plain wins");
}

TEST_CASE("Linux: Wayland-style offer (text/plain;charset=utf-8, no UTF8_STRING)",
          "[clipboard][linux-select]") {
  FakeClipboard clipboard;
  clipboard.offer("text/html", "<p>loses</p>").offer("text/plain;charset=UTF-8", "plain\r\ntext");
  REQUIRE(mime_clipboard_plain_text(clipboard) == std::string("plain\ntext"));
  // Asked for exactly as offered: X11 target names are case-sensitive.
  REQUIRE(clipboard.requests ==
          std::vector<std::string>{"TARGETS", "text/plain;charset=UTF-8"});
}

TEST_CASE("Linux: a URI list is the last resort", "[clipboard][linux-select]") {
  FakeClipboard clipboard;
  clipboard.offer("text/uri-list", "# copied\r\nfile:///tmp/a.txt\r\nhttps://example.com/\r\n");
  REQUIRE(mime_clipboard_plain_text(clipboard) ==
          std::string("file:///tmp/a.txt\nhttps://example.com/"));
  REQUIRE(paste_into_empty_buffer(clipboard) == "file:///tmp/a.txt\nhttps://example.com/");
}

TEST_CASE("Linux: an empty clipboard offers nothing and pasting it is a no-op",
          "[clipboard][linux-select]") {
  FakeClipboard no_owner;
  no_owner.has_owner = false;
  REQUIRE(mime_clipboard_plain_text(no_owner) == std::nullopt);
  REQUIRE(no_owner.requests == std::vector<std::string>{"TARGETS"});
  REQUIRE(paste_into_empty_buffer(no_owner).empty());

  // An owner offering only types that convert to nothing.
  FakeClipboard empty;
  empty.offer("UTF8_STRING", "").offer("text/html", "<p> </p>").offer("image/png", "\x89PNG");
  REQUIRE(mime_clipboard_plain_text(empty) == std::nullopt);
  REQUIRE(paste_into_empty_buffer(empty).empty());
  Editor ed;
  attach(ed, empty);
  REQUIRE_FALSE(ed.can_paste());  // Edit > Paste greyed out
}

TEST_CASE("Linux: an owner that never answers costs one timeout, then paste uses the register",
          "[clipboard][linux-select]") {
  // TARGETS itself times out (hung app): nothing else is asked.
  FakeClipboard hung;
  hung.hang_on_targets = true;
  hung.offer("UTF8_STRING", "never delivered");
  REQUIRE(mime_clipboard_plain_text(hung) == std::nullopt);
  REQUIRE(hung.requests == std::vector<std::string>{"TARGETS"});

  // The HTML transfer times out: RTF and the URI list aren't tried after it.
  FakeClipboard stalls;
  stalls.offer("text/html", kChatHtml).offer("text/rtf", kRtf).offer("text/uri-list", "x:y");
  stalls.hang_on = {"text/html"};
  REQUIRE(mime_clipboard_plain_text(stalls) == std::nullopt);
  REQUIRE(stalls.requests == std::vector<std::string>{"TARGETS", "text/html"});

  // The paste falls back to zedit's own register instead of hanging.
  Editor ed;
  attach(ed, hung);
  feed(ed, "iab\x1b");
  feed(ed, "0yl");  // yank "a"
  feed(ed, "$p");
  REQUIRE(ed.buffer().to_string() == "aba");
}

TEST_CASE("Linux: owner that refuses TARGETS still pastes its plain text",
          "[clipboard][linux-select]") {
  FakeClipboard old_owner;
  old_owner.refuses_targets = true;
  old_owner.offer("STRING", "caf\xE9");  // ISO-8859-1, as STRING is defined
  REQUIRE(mime_clipboard_plain_text(old_owner) == std::string("caf\xC3\xA9"));
  REQUIRE(old_owner.requests ==
          std::vector<std::string>{"TARGETS", "UTF8_STRING", "text/plain;charset=utf-8",
                                   "text/plain", "STRING"});
}

TEST_CASE("Linux: an empty plain-text flavor falls through to the HTML",
          "[clipboard][linux-select]") {
  FakeClipboard clipboard;
  clipboard.offer("UTF8_STRING", "").offer("text/html", "<ol><li>one</li><li>two</li></ol>");
  REQUIRE(mime_clipboard_plain_text(clipboard) == std::string("1. one\n2. two"));
}

TEST_CASE("Linux: clipboard bytes are decoded to UTF-8", "[clipboard][linux-select]") {
  // Firefox puts text/html on the X11 clipboard as UTF-16 with a BOM.
  std::string utf16le("\xFF\xFE", 2);
  for (char c : std::string_view("<p>Caf")) utf16le += std::string{c, '\0'};
  utf16le += std::string("\xE9\x00", 2);                 // U+00E9
  utf16le += std::string("\x3D\xD8\x00\xDE", 4);         // U+1F600 as a surrogate pair
  for (char c : std::string_view("</p>")) utf16le += std::string{c, '\0'};
  REQUIRE(decode_clipboard_text("text/html", utf16le) == "<p>Caf\xC3\xA9\xF0\x9F\x98\x80</p>");
  FakeClipboard firefox;
  firefox.offer("text/html", utf16le);
  REQUIRE(mime_clipboard_plain_text(firefox) == std::string("Caf\xC3\xA9\xF0\x9F\x98\x80"));

  std::string utf16be("\xFE\xFF\x00h\x00i", 6);
  REQUIRE(decode_clipboard_text("text/html", utf16be) == "hi");
  REQUIRE(decode_clipboard_text("text/html", std::string("h\0i\0", 4)) == "hi");
  REQUIRE(decode_clipboard_text("text/plain", "\xEF\xBB\xBFhi") == "hi");  // UTF-8 BOM
  REQUIRE(decode_clipboard_text("UTF8_STRING", std::string("hi\0", 3)) == "hi");  // trailing NUL
  REQUIRE(decode_clipboard_text("STRING", "\xE9") == "\xC3\xA9");
  REQUIRE(decode_clipboard_text("text/plain", "caf\xE9") == "caf\xC3\xA9");  // not UTF-8
  REQUIRE(decode_clipboard_text("UTF8_STRING", "caf\xC3\xA9") == "caf\xC3\xA9");
}
