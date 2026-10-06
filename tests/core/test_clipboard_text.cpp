// Pasting rich-text clipboard contents (HTML-only / RTF-only copies) as
// plain text. The frontend fills ClipboardFlavors from the real clipboard
// (macOS: NSPasteboard) and hands Editor clipboard_plain_text() as its
// ClipboardBridge read callback, exactly as wired here.

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>

#include "zedit/core/clipboard_text.hpp"
#include "zedit/core/editor.hpp"

using zedit::core::clipboard_has_text;
using zedit::core::clipboard_plain_text;
using zedit::core::ClipboardFlavors;
using zedit::core::Editor;
using zedit::core::html_to_plain_text;
using zedit::core::Key;
using zedit::core::KeyEvent;
using zedit::core::rtf_to_plain_text;
using zedit::core::uri_list_to_plain_text;

namespace {

// What an Electron chat app put on the clipboard for a selection holding a
// paragraph, a numbered list with inline code, and a closing line -- with
// no plain-text flavor alongside it.
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

// The shape of RTF that Cocoa apps (TextEdit, Notes, Mail...) write: font,
// color and list tables, Cocoa's backslash-newline paragraph breaks,
// {\listtext} list labels, a \'hh escape and a \uN code point.
constexpr std::string_view kCocoaRtf =
    "{\\rtf1\\ansi\\ansicpg1252\\cocoartf2761\n"
    "\\cocoatextscaling0\\cocoaplatform0{\\fonttbl\\f0\\fswiss\\fcharset0 Helvetica;"
    "\\f1\\fmodern\\fcharset0 Menlo-Regular;}\n"
    "{\\colortbl;\\red255\\green255\\blue255;}\n"
    "{\\*\\expandedcolortbl;;}\n"
    "{\\*\\listtable{\\list\\listtemplateid1\\listhybrid{\\listlevel\\levelnfc0\\levelstartat1"
    "{\\*\\levelmarker \\{decimal\\}.}{\\leveltext\\leveltemplateid1\\'02\\'00.;}"
    "{\\levelnumbers\\'01;}\\fi-360\\li720\\lin720 }{\\listname ;}\\listid1}}\n"
    "{\\*\\listoverridetable{\\listoverride\\listid1\\listoverridecount0\\ls1}}\n"
    "\\paperw11900\\paperh16840\\margl1440\\margr1440\\vieww11520\\viewh8400\\viewkind0\n"
    "\\pard\\tx220\\tx720\\pardirnatural\\partightenfactor0\n"
    "\\ls1\\ilvl0\\f0\\fs24 \\cf0 {\\listtext\t1.\t}Run \n"
    "\\f1 make test\n"
    "\\f0 \\\n"
    "{\\listtext\t2.\t}Caf\\'e9 \\uc0\\u8212  done}";
constexpr std::string_view kCocoaText = "1. Run make test\n2. Caf\xC3\xA9 \xE2\x80\x94 done";

void attach(Editor& ed, const ClipboardFlavors& flavors) {
  ed.set_clipboard_bridge(Editor::ClipboardBridge{
      [&flavors] { return clipboard_plain_text(flavors); },
      [](const std::string&) {},
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

// Paste via Insert-mode Ctrl-P (what Cmd-V / Edit > Paste send) into an
// empty buffer and return the buffer.
std::string paste_into_empty_buffer(const ClipboardFlavors& flavors) {
  Editor ed;
  attach(ed, flavors);
  feed(ed, "i");
  press(ed, Key::CtrlP);
  return ed.buffer().to_string();
}

}  // namespace

// ---------------------------------------------------------------------------
// The four clipboard shapes from the bug report
// ---------------------------------------------------------------------------

TEST_CASE("HTML-only clipboard (numbered list + inline code) pastes as plain text",
          "[clipboard][paste-rich]") {
  ClipboardFlavors flavors;
  flavors.html = std::string(kChatHtml);
  REQUIRE(clipboard_has_text(flavors));
  REQUIRE(clipboard_plain_text(flavors) == std::string(kChatText));
  REQUIRE(paste_into_empty_buffer(flavors) == kChatText);
}

TEST_CASE("RTF-only clipboard pastes as plain text", "[clipboard][paste-rich]") {
  ClipboardFlavors flavors;
  flavors.rtf = std::string(kCocoaRtf);
  REQUIRE(clipboard_has_text(flavors));
  REQUIRE(clipboard_plain_text(flavors) == std::string(kCocoaText));
  REQUIRE(paste_into_empty_buffer(flavors) == kCocoaText);
}

TEST_CASE("Plain text wins over HTML and RTF when both are offered", "[clipboard][paste-rich]") {
  ClipboardFlavors flavors;
  flavors.plain_text = "plain wins";
  flavors.html = "<b>html loses</b>";
  flavors.rtf = "{\\rtf1 rtf loses}";
  REQUIRE(clipboard_has_text(flavors));
  REQUIRE(clipboard_plain_text(flavors) == std::string("plain wins"));
  REQUIRE(paste_into_empty_buffer(flavors) == "plain wins");
}

TEST_CASE("An empty clipboard offers nothing and pasting it is a no-op",
          "[clipboard][paste-rich]") {
  ClipboardFlavors nothing;
  REQUIRE_FALSE(clipboard_has_text(nothing));
  REQUIRE(clipboard_plain_text(nothing) == std::nullopt);
  REQUIRE(paste_into_empty_buffer(nothing).empty());

  ClipboardFlavors empty_flavors;
  empty_flavors.plain_text = "";
  empty_flavors.html = "<p> </p>";
  empty_flavors.rtf = "{\\rtf1\\ansi{\\fonttbl\\f0 Helvetica;}}";
  empty_flavors.uri_list = "# just a comment\n";
  REQUIRE_FALSE(clipboard_has_text(empty_flavors));
  REQUIRE(clipboard_plain_text(empty_flavors) == std::nullopt);
  REQUIRE(paste_into_empty_buffer(empty_flavors).empty());
}

// ---------------------------------------------------------------------------
// Paste paths and Edit > Paste's enabled state
// ---------------------------------------------------------------------------

TEST_CASE("Normal-mode p pastes an HTML-only clipboard as plain text",
          "[clipboard][paste-rich]") {
  Editor ed;
  ClipboardFlavors flavors;
  flavors.html = "<span style=\"font-weight:bold\">bold</span> <i>words</i>";
  attach(ed, flavors);
  feed(ed, "i>\x1b");
  feed(ed, "p");
  REQUIRE(ed.buffer().to_string() == ">bold words");
}

TEST_CASE("can_paste follows the same rule as the paste itself", "[clipboard][paste-rich]") {
  Editor ed;
  ClipboardFlavors flavors;
  attach(ed, flavors);
  REQUIRE_FALSE(ed.can_paste());  // nothing on the clipboard, empty register

  flavors.html = "<ol><li>one</li></ol>";
  REQUIRE(ed.can_paste());
  flavors.html.reset();
  flavors.rtf = "{\\rtf1 hi}";
  REQUIRE(ed.can_paste());
  flavors.rtf.reset();
  REQUIRE_FALSE(ed.can_paste());

  // zedit's own yank is pasteable even with an empty system clipboard.
  feed(ed, "iab\x1b");
  feed(ed, "0yl");
  REQUIRE(ed.can_paste());
}

TEST_CASE("can_paste with no clipboard bridge reflects the unnamed register",
          "[clipboard][paste-rich]") {
  Editor ed;
  REQUIRE_FALSE(ed.can_paste());
  feed(ed, "iab\x1b");
  feed(ed, "0yl");
  REQUIRE(ed.can_paste());
}

TEST_CASE("Windows line endings in clipboard text are normalized", "[clipboard][paste-rich]") {
  ClipboardFlavors flavors;
  flavors.plain_text = "one\r\ntwo\rthree";
  REQUIRE(clipboard_plain_text(flavors) == std::string("one\ntwo\nthree"));
}

TEST_CASE("A URI list is the last-resort text flavor", "[clipboard][paste-rich]") {
  ClipboardFlavors flavors;
  flavors.uri_list = "# comment\r\nfile:///tmp/a.txt\r\nhttps://example.com/\r\n";
  REQUIRE(clipboard_plain_text(flavors) == std::string("file:///tmp/a.txt\nhttps://example.com/"));
  REQUIRE(uri_list_to_plain_text("https://x.test/") == "https://x.test/");
}

// ---------------------------------------------------------------------------
// HTML -> text details
// ---------------------------------------------------------------------------

TEST_CASE("HTML: whitespace collapses, <br> and blocks break lines", "[clipboard][html]") {
  REQUIRE(html_to_plain_text("  hello \n\t  world  ") == "hello world");
  REQUIRE(html_to_plain_text("a<br>b<br/><br />c") == "a\nb\n\nc");
  REQUIRE(html_to_plain_text("<div>one</div><div>two</div>") == "one\ntwo");
  REQUIRE(html_to_plain_text("<p>one</p><p>two</p>") == "one\n\ntwo");
  REQUIRE(html_to_plain_text("<h2>Title</h2>body") == "Title\n\nbody");
  REQUIRE(html_to_plain_text("<B>x</B> <SPAN>y</SPAN>") == "x y");
}

TEST_CASE("HTML: entities decode, unknown ones stay literal", "[clipboard][html]") {
  REQUIRE(html_to_plain_text("&lt;tag&gt; &amp; &quot;q&quot; &#39;s&#x27;") ==
          "<tag> & \"q\" 's'");
  REQUIRE(html_to_plain_text("caf&#233; &#x1F600;") == "caf\xC3\xA9 \xF0\x9F\x98\x80");
  REQUIRE(html_to_plain_text("a &bogus; b & c") == "a &bogus; b & c");
  REQUIRE(html_to_plain_text("a&nbsp;&nbsp;b") == "a  b");
  REQUIRE(html_to_plain_text("1 < 2") == "1 < 2");
}

TEST_CASE("HTML: list numbering, start/value attributes, bullets and nesting",
          "[clipboard][html]") {
  REQUIRE(html_to_plain_text("<ol start=\"3\"><li>c</li><li>d</li></ol>") == "3. c\n4. d");
  REQUIRE(html_to_plain_text("<ol><li>a<li value=7>g<li>h</ol>") == "1. a\n7. g\n8. h");
  REQUIRE(html_to_plain_text("<ul><li>x</li><li>y</li></ul>") == "- x\n- y");
  REQUIRE(html_to_plain_text("<ol><li>top<ul><li>inner</li></ul></li><li>next</li></ol>") ==
          "1. top\n   - inner\n2. next");
  // ChatGPT/Notion-style <p> inside <li> stays a tight list.
  REQUIRE(html_to_plain_text("<ol><li><p>one</p></li><li><p>two</p></li></ol>") ==
          "1. one\n2. two");
}

TEST_CASE("HTML: <pre> keeps whitespace and line breaks", "[clipboard][html]") {
  REQUIRE(html_to_plain_text("<p>Code:</p><pre><code>int main() {\n  return 0;\n}\n</code></pre>"
                             "<p>after</p>") ==
          "Code:\n\nint main() {\n  return 0;\n}\n\nafter");
}

TEST_CASE("HTML: head, script, style and comments are skipped", "[clipboard][html]") {
  REQUIRE(html_to_plain_text("<html><head><title>T</title><style>p{color:red}</style></head>"
                             "<body><!--StartFragment--><SCRIPT>alert(1)</SCRIPT>visible"
                             "<!--EndFragment--></body></html>") == "visible");
}

TEST_CASE("HTML: Windows CF_HTML header is ignored", "[clipboard][html]") {
  REQUIRE(html_to_plain_text("Version:0.9\r\nStartHTML:0000000105\r\nEndHTML:0000000199\r\n"
                             "<html><body><!--StartFragment-->hi there<!--EndFragment-->"
                             "</body></html>") == "hi there");
}

TEST_CASE("HTML: table cells become tabs, rows become lines", "[clipboard][html]") {
  REQUIRE(html_to_plain_text("<table><tr><th>a</th><th>b</th></tr><tr><td>1</td><td>2</td></tr>"
                             "</table>") == "a\tb\n1\t2");
}

// ---------------------------------------------------------------------------
// RTF -> text details
// ---------------------------------------------------------------------------

TEST_CASE("RTF: paragraph, line and tab controls", "[clipboard][rtf]") {
  REQUIRE(rtf_to_plain_text("{\\rtf1\\ansi one\\par two\\line three\\tab four}") ==
          "one\ntwo\nthree\tfour");
}

TEST_CASE("RTF: escapes and hex bytes", "[clipboard][rtf]") {
  REQUIRE(rtf_to_plain_text("{\\rtf1 a\\\\b \\{c\\} na\\'efve \\'93q\\'94}") ==
          "a\\b {c} na\xC3\xAFve \xE2\x80\x9Cq\xE2\x80\x9D");
}

TEST_CASE("RTF: \\uN with fallback characters and surrogate pairs", "[clipboard][rtf]") {
  REQUIRE(rtf_to_plain_text("{\\rtf1 \\u8364?5 and \\uc2\\u8212\\'97\\'97 x}") ==
          "\xE2\x82\xAC" "5 and \xE2\x80\x94 x");
  REQUIRE(rtf_to_plain_text("{\\rtf1 smile \\u-10179?\\u-8704?}") == "smile \xF0\x9F\x98\x80");
}

TEST_CASE("RTF: non-text destinations are skipped", "[clipboard][rtf]") {
  REQUIRE(rtf_to_plain_text("{\\rtf1{\\fonttbl{\\f0 Arial;}}{\\colortbl;\\red0\\green0\\blue0;}"
                            "{\\stylesheet{\\s0 Normal;}}{\\info{\\author Me}}"
                            "{\\*\\generator Word}{\\pict\\pngblip 89504e47}"
                            "{\\field{\\*\\fldinst HYPERLINK \"https://x\"}{\\fldrslt link}} text}") ==
          "link text");
}

TEST_CASE("RTF: Word-style \\pntext list labels", "[clipboard][rtf]") {
  REQUIRE(rtf_to_plain_text("{\\rtf1{\\pntext\\f0 1.\\tab}first\\par{\\pntext\\f0 2.\\tab}second}") ==
          "1. first\n2. second");
}
