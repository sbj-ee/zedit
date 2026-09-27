#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>

#include "zedit/core/editor.hpp"

using zedit::core::Cursor;
using zedit::core::EditingStyle;
using zedit::core::Editor;
using zedit::core::Key;
using zedit::core::KeyEvent;

namespace {

// Stands in for the desktop clipboard the frontend wires up via GLFW.
struct FakeClipboard {
  std::optional<std::string> text;
  int writes = 0;
};

void attach(Editor& ed, FakeClipboard& clip) {
  ed.set_clipboard_bridge(Editor::ClipboardBridge{
      [&clip] { return clip.text; },
      [&clip](const std::string& t) {
        clip.text = t;
        ++clip.writes;
      },
  });
}

void press(Editor& ed, Key key) { ed.handle_key(KeyEvent{key, 0}); }

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

}  // namespace

TEST_CASE("A yank is pushed to the system clipboard", "[clipboard]") {
  Editor ed;
  FakeClipboard clip;
  attach(ed, clip);
  feed(ed, "ihello world\x1b");
  feed(ed, "0yw");
  REQUIRE(clip.text == "hello ");
}

TEST_CASE("Text copied in another app is what the next p pastes", "[clipboard]") {
  Editor ed;
  FakeClipboard clip;
  attach(ed, clip);
  feed(ed, "iab\x1b");
  feed(ed, "0yl");  // register (and clipboard) now "a"
  clip.text = "XYZ";  // user copies in a browser
  feed(ed, "$p");
  REQUIRE(ed.buffer().to_string() == "abXYZ");
}

TEST_CASE("Insert-mode Ctrl-P pastes the external clipboard at the cursor", "[clipboard]") {
  Editor ed;
  FakeClipboard clip;
  attach(ed, clip);
  clip.text = "The quick brown fox\n";
  feed(ed, "i>");
  press(ed, Key::CtrlP);
  REQUIRE(ed.buffer().to_string() == ">The quick brown fox\n");
}

TEST_CASE("External text ending in a newline pastes linewise with p", "[clipboard]") {
  Editor ed;
  FakeClipboard clip;
  attach(ed, clip);
  feed(ed, "ione\ntwo\x1b");
  ed.set_cursor(Cursor{0, 0});
  clip.text = "new\n";
  feed(ed, "p");
  REQUIRE(ed.buffer().to_string() == "one\nnew\ntwo");
}

TEST_CASE("zedit's own linewise yank keeps its linewise flag across the round trip",
          "[clipboard]") {
  Editor ed;
  FakeClipboard clip;
  attach(ed, clip);
  feed(ed, "ione\ntwo\x1b");
  ed.set_cursor(Cursor{0, 0});
  feed(ed, "yyjp");
  REQUIRE(ed.buffer().to_string() == "one\ntwo\none");
}

TEST_CASE("Named registers neither push to nor read from the clipboard", "[clipboard]") {
  Editor ed;
  FakeClipboard clip;
  attach(ed, clip);
  feed(ed, "ione\x1b");
  clip.text = "external";
  int writes_before = clip.writes;
  feed(ed, "\"ayy");
  // The unnamed register mirrors "a, as in vim, so the clipboard follows.
  REQUIRE(clip.writes == writes_before + 1);
  clip.text = "external again";
  feed(ed, "\"ap");
  REQUIRE(ed.buffer().to_string() == "one\none");
}

TEST_CASE("An empty or unavailable clipboard leaves the register alone", "[clipboard]") {
  Editor ed;
  FakeClipboard clip;
  attach(ed, clip);
  feed(ed, "iab\x1b");
  feed(ed, "0yl");
  clip.text = std::nullopt;
  feed(ed, "$p");
  REQUIRE(ed.buffer().to_string() == "aba");
}

TEST_CASE("With no bridge attached, registers work as before", "[clipboard]") {
  Editor ed;
  feed(ed, "iab\x1b");
  feed(ed, "0ylp");
  REQUIRE(ed.buffer().to_string() == "aab");
}

TEST_CASE("Gedit style: Ctrl-P over a selection pastes the clipboard, not the selection",
          "[clipboard][gedit-style]") {
  Editor ed;
  FakeClipboard clip;
  attach(ed, clip);
  ed.set_editing_style(EditingStyle::Gedit);
  feed(ed, "hello world");
  ed.set_cursor(Cursor{0, 0});
  for (int i = 0; i < 4; ++i) {
    press(ed, Key::ShiftRight);  // select "hello"
  }
  clip.text = "howdy";
  press(ed, Key::CtrlP);
  REQUIRE(ed.buffer().to_string() == "howdy world");
  REQUIRE(clip.text == "howdy");
}

TEST_CASE("Gedit style: typing over a selection doesn't touch the clipboard",
          "[clipboard][gedit-style]") {
  Editor ed;
  FakeClipboard clip;
  attach(ed, clip);
  ed.set_editing_style(EditingStyle::Gedit);
  feed(ed, "hello world");
  clip.text = "copied elsewhere";
  int writes_before = clip.writes;
  ed.set_cursor(Cursor{0, 0});
  for (int i = 0; i < 4; ++i) {
    press(ed, Key::ShiftRight);
  }
  feed(ed, "J");
  REQUIRE(ed.buffer().to_string() == "J world");
  REQUIRE(clip.text == "copied elsewhere");
  REQUIRE(clip.writes == writes_before);
}
