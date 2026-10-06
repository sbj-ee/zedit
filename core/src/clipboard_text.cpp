#include "zedit/core/clipboard_text.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>

namespace zedit::core {

namespace {

void append_utf8(std::string& out, uint32_t cp) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    cp = 0xFFFD;
  }
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

bool is_html_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

char ascii_lower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = ascii_lower(c);
  return out;
}

// "\r\n" and lone "\r" become "\n".
std::string normalize_newlines(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\r') {
      out += '\n';
      if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
    } else {
      out += s[i];
    }
  }
  return out;
}

// Drops trailing spaces/tabs on every line, then leading and trailing
// blank lines -- the tidy-up both rich-text converters share.
std::string tidy_converted_text(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  size_t line_start = 0;
  while (line_start <= s.size()) {
    size_t nl = s.find('\n', line_start);
    size_t end = nl == std::string_view::npos ? s.size() : nl;
    std::string_view line = s.substr(line_start, end - line_start);
    size_t keep = line.find_last_not_of(" \t");
    out.append(line.substr(0, keep == std::string_view::npos ? 0 : keep + 1));
    if (nl == std::string_view::npos) break;
    out += '\n';
    line_start = nl + 1;
  }
  size_t first = out.find_first_not_of('\n');
  if (first == std::string::npos) return {};
  size_t last = out.find_last_not_of('\n');
  return out.substr(first, last - first + 1);
}

// ---------------------------------------------------------------------------
// HTML
// ---------------------------------------------------------------------------

struct NamedEntity {
  std::string_view name;
  uint32_t cp;
};

constexpr std::array<NamedEntity, 32> kNamedEntities{{
    {"amp", '&'},       {"lt", '<'},         {"gt", '>'},         {"quot", '"'},
    {"apos", '\''},     {"nbsp", 0xA0},      {"ensp", 0x2002},    {"emsp", 0x2003},
    {"thinsp", 0x2009}, {"ndash", 0x2013},   {"mdash", 0x2014},   {"lsquo", 0x2018},
    {"rsquo", 0x2019},  {"sbquo", 0x201A},   {"ldquo", 0x201C},   {"rdquo", 0x201D},
    {"bdquo", 0x201E},  {"bull", 0x2022},    {"hellip", 0x2026},  {"prime", 0x2032},
    {"laquo", 0xAB},    {"raquo", 0xBB},     {"middot", 0xB7},    {"copy", 0xA9},
    {"reg", 0xAE},      {"trade", 0x2122},   {"deg", 0xB0},       {"times", 0xD7},
    {"divide", 0xF7},   {"euro", 0x20AC},    {"larr", 0x2190},    {"rarr", 0x2192},
}};

// Decodes the character reference starting at html[i] == '&'. On success
// returns the code point and advances i past the reference; otherwise
// returns nullopt and leaves i alone (the '&' is then literal text).
std::optional<uint32_t> decode_entity(std::string_view html, size_t& i) {
  size_t j = i + 1;
  if (j < html.size() && html[j] == '#') {
    ++j;
    int base = 10;
    if (j < html.size() && (html[j] == 'x' || html[j] == 'X')) {
      base = 16;
      ++j;
    }
    size_t digits_start = j;
    uint32_t value = 0;
    auto [ptr, ec] = std::from_chars(html.data() + j, html.data() + html.size(), value, base);
    if (ec != std::errc() || ptr == html.data() + digits_start) return std::nullopt;
    j = static_cast<size_t>(ptr - html.data());
    if (j < html.size() && html[j] == ';') ++j;
    i = j;
    if (value == 0) return 0xFFFD;
    return value;
  }
  size_t name_start = j;
  while (j < html.size() && (is_alpha(html[j]) || is_digit(html[j])) && j - name_start < 10) ++j;
  if (j == name_start || j >= html.size() || html[j] != ';') return std::nullopt;
  std::string_view name = html.substr(name_start, j - name_start);
  for (const NamedEntity& e : kNamedEntities) {
    if (e.name == name) {
      i = j + 1;
      return e.cp;
    }
  }
  return std::nullopt;
}

using Attributes = std::vector<std::pair<std::string, std::string>>;

std::optional<long> int_attribute(const Attributes& attrs, std::string_view name) {
  for (const auto& [key, value] : attrs) {
    if (key != name) continue;
    std::string_view v = value;
    while (!v.empty() && is_html_space(v.front())) v.remove_prefix(1);
    long n = 0;
    auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
    if (ec == std::errc() && ptr != v.data()) return n;
  }
  return std::nullopt;
}

bool is_block_element(std::string_view tag) {
  static constexpr std::array<std::string_view, 30> kBlocks{
      "address", "article", "aside",   "blockquote", "center",  "dd",
      "details", "dialog",  "div",     "dl",         "dt",      "fieldset",
      "figcaption", "figure", "footer", "form",      "header",  "hgroup",
      "hr",      "main",    "nav",     "section",    "summary", "table",
      "caption", "thead",   "tbody",   "tfoot",      "option",  "menu"};
  return std::find(kBlocks.begin(), kBlocks.end(), tag) != kBlocks.end();
}

bool is_paragraph_like(std::string_view tag) {
  return tag == "p" || (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6');
}

// Elements whose whole content is never rendered as text.
bool is_skipped_element(std::string_view tag) {
  return tag == "head" || tag == "script" || tag == "style" || tag == "title" ||
         tag == "template" || tag == "noscript" || tag == "svg" || tag == "math";
}

// Position of the first "</tag" (case-insensitive) at or after `from`.
size_t find_close_tag(std::string_view html, size_t from, std::string_view tag) {
  for (size_t k = html.find("</", from); k != std::string_view::npos; k = html.find("</", k + 2)) {
    if (k + 2 + tag.size() > html.size()) break;
    bool match = true;
    for (size_t t = 0; t < tag.size() && match; ++t) {
      match = ascii_lower(html[k + 2 + t]) == tag[t];
    }
    if (match) return k;
  }
  return std::string_view::npos;
}

class HtmlRenderer {
 public:
  std::string run(std::string_view html) {
    // Windows CF_HTML carries a "Version:0.9\r\nStartHTML:..." header in
    // front of the markup; it isn't part of the content.
    if (html.substr(0, 8) == "Version:") {
      size_t lt = html.find('<');
      html = lt == std::string_view::npos ? std::string_view{} : html.substr(lt);
    }
    size_t i = 0;
    while (i < html.size()) {
      char c = html[i];
      if (c == '<') {
        if (parse_markup(html, i)) continue;
        text_char('<');
        ++i;
      } else if (c == '&') {
        if (std::optional<uint32_t> cp = decode_entity(html, i)) {
          text_code_point(*cp);
        } else {
          text_char('&');
          ++i;
        }
      } else if (static_cast<unsigned char>(c) == 0xC2 && i + 1 < html.size() &&
                 static_cast<unsigned char>(html[i + 1]) == 0xA0) {
        text_code_point(0xA0);  // a literal UTF-8 no-break space
        i += 2;
      } else {
        text_char(c);
        ++i;
      }
    }
    return tidy_converted_text(out_);
  }

 private:
  struct ListFrame {
    bool ordered = false;
    long next = 1;
    bool li_open = false;
  };

  // ---- output -----------------------------------------------------------

  void require_breaks(int n) {
    required_breaks_ = std::max(required_breaks_, n);
    pending_space_ = false;
  }

  void strip_trailing_spaces() {
    if (pre_depth_ > 0) return;
    while (!out_.empty() && out_.back() == ' ') out_.pop_back();
  }

  void flush_breaks() {
    if (required_breaks_ > 0 && !out_.empty()) {
      strip_trailing_spaces();
      int have = 0;
      for (auto it = out_.rbegin(); it != out_.rend() && *it == '\n'; ++it) ++have;
      for (int k = have; k < required_breaks_; ++k) out_ += '\n';
      at_line_start_ = true;
    }
    required_breaks_ = 0;
  }

  // Called before any visible character: settles pending line breaks, then
  // the list indentation/marker at a line start, or a collapsed space.
  void begin_visible() {
    flush_breaks();
    if (at_line_start_) {
      size_t indent = std::accumulate(indent_stack_.begin(), indent_stack_.end(), size_t{0});
      if (!pending_marker_.empty()) indent -= std::min(indent, pending_marker_.size());
      out_.append(indent, ' ');
      out_ += pending_marker_;
      pending_marker_.clear();
      at_line_start_ = false;
      pending_space_ = false;
    } else if (pending_space_) {
      out_ += ' ';
      pending_space_ = false;
    }
  }

  void visible(std::string_view s) {
    begin_visible();
    out_ += s;
  }

  void hard_newline() {
    flush_breaks();
    if (!pending_marker_.empty()) begin_visible();
    strip_trailing_spaces();
    out_ += '\n';
    at_line_start_ = true;
    pending_space_ = false;
  }

  void text_char(char c) {
    if (pre_depth_ > 0) {
      if (c == '\r') return;  // "\r\n" -> the '\n' does the work
      if (c == '\n') {
        if (skip_lf_after_pre_) {
          skip_lf_after_pre_ = false;
          return;
        }
        hard_newline();
        return;
      }
      skip_lf_after_pre_ = false;
      visible(std::string_view(&c, 1));
      return;
    }
    if (is_html_space(c)) {
      if (!at_line_start_ && required_breaks_ == 0) pending_space_ = true;
      return;
    }
    visible(std::string_view(&c, 1));
  }

  void text_code_point(uint32_t cp) {
    skip_lf_after_pre_ = false;
    if (cp == 0xA0) cp = ' ';  // a code editor wants a plain space, but an uncollapsed one
    std::string s;
    append_utf8(s, cp);
    visible(s);
  }

  // ---- markup -----------------------------------------------------------

  // Handles the markup starting at html[i] == '<'. Returns false when it
  // isn't markup at all (e.g. "a < b"), leaving i untouched.
  bool parse_markup(std::string_view html, size_t& i) {
    if (html.substr(i, 4) == "<!--") {
      size_t end = html.find("-->", i + 4);
      i = end == std::string_view::npos ? html.size() : end + 3;
      return true;
    }
    if (i + 1 < html.size() && (html[i + 1] == '!' || html[i + 1] == '?')) {
      size_t end = html.find('>', i);
      i = end == std::string_view::npos ? html.size() : end + 1;
      return true;
    }
    size_t j = i + 1;
    bool closing = false;
    if (j < html.size() && html[j] == '/') {
      closing = true;
      ++j;
    }
    if (j >= html.size() || !is_alpha(html[j])) return false;
    size_t name_start = j;
    while (j < html.size() && (is_alpha(html[j]) || is_digit(html[j]) || html[j] == '-' ||
                               html[j] == ':')) {
      ++j;
    }
    std::string tag = lower(html.substr(name_start, j - name_start));
    Attributes attrs;
    j = parse_attributes(html, j, attrs);
    i = j;

    if (!closing && is_skipped_element(tag)) {
      // Skip to the matching close tag (case-insensitively).
      size_t end = find_close_tag(html, i, tag);
      if (end == std::string_view::npos) {
        i = html.size();
      } else {
        size_t gt = html.find('>', end);
        i = gt == std::string_view::npos ? html.size() : gt + 1;
      }
      return true;
    }
    if (closing) {
      close_tag(tag);
    } else {
      open_tag(tag, attrs);
    }
    return true;
  }

  static size_t parse_attributes(std::string_view html, size_t j, Attributes& attrs) {
    while (j < html.size()) {
      while (j < html.size() && is_html_space(html[j])) ++j;
      if (j >= html.size()) break;
      if (html[j] == '>') return j + 1;
      if (html[j] == '/') {
        ++j;
        continue;
      }
      size_t name_start = j;
      while (j < html.size() && !is_html_space(html[j]) && html[j] != '=' && html[j] != '>' &&
             html[j] != '/') {
        ++j;
      }
      std::string name = lower(html.substr(name_start, j - name_start));
      while (j < html.size() && is_html_space(html[j])) ++j;
      std::string value;
      if (j < html.size() && html[j] == '=') {
        ++j;
        while (j < html.size() && is_html_space(html[j])) ++j;
        if (j < html.size() && (html[j] == '"' || html[j] == '\'')) {
          char quote = html[j++];
          size_t end = html.find(quote, j);
          if (end == std::string_view::npos) end = html.size();
          value = std::string(html.substr(j, end - j));
          j = end < html.size() ? end + 1 : end;
        } else {
          size_t value_start = j;
          while (j < html.size() && !is_html_space(html[j]) && html[j] != '>') ++j;
          value = std::string(html.substr(value_start, j - value_start));
        }
      }
      if (name.empty()) {
        ++j;  // stray character; make progress
        continue;
      }
      attrs.emplace_back(std::move(name), std::move(value));
    }
    return j;
  }

  void close_open_item(ListFrame& frame) {
    if (!frame.li_open) return;
    if (!pending_marker_.empty()) begin_visible();  // an empty <li> still shows its number
    if (!indent_stack_.empty()) indent_stack_.pop_back();
    frame.li_open = false;
  }

  void open_tag(const std::string& tag, const Attributes& attrs) {
    if (tag == "br") {
      hard_newline();
    } else if (tag == "ol" || tag == "ul") {
      if (!pending_marker_.empty()) begin_visible();
      require_breaks(1);
      ListFrame frame;
      frame.ordered = tag == "ol";
      frame.next = int_attribute(attrs, "start").value_or(1);
      lists_.push_back(frame);
    } else if (tag == "li") {
      if (lists_.empty()) lists_.push_back(ListFrame{});  // stray <li>: treat as <ul>
      ListFrame& frame = lists_.back();
      close_open_item(frame);
      if (!pending_marker_.empty()) begin_visible();
      require_breaks(1);
      std::string marker = "- ";
      if (frame.ordered) {
        long n = int_attribute(attrs, "value").value_or(frame.next);
        frame.next = n + 1;
        marker = std::to_string(n) + ". ";
      }
      indent_stack_.push_back(marker.size());
      pending_marker_ = std::move(marker);
      frame.li_open = true;
    } else if (tag == "pre" || tag == "listing" || tag == "xmp") {
      require_breaks(1);
      ++pre_depth_;
      skip_lf_after_pre_ = true;
    } else if (tag == "tr") {
      require_breaks(1);
      cell_index_ = 0;
    } else if (tag == "td" || tag == "th") {
      if (cell_index_ > 0) {
        pending_space_ = false;
        visible("\t");
      }
      ++cell_index_;
    } else if (is_paragraph_like(tag)) {
      require_breaks(indent_stack_.empty() ? 2 : 1);
    } else if (is_block_element(tag)) {
      require_breaks(1);
    }
  }

  void close_tag(const std::string& tag) {
    if (tag == "ol" || tag == "ul") {
      if (!lists_.empty()) {
        close_open_item(lists_.back());
        lists_.pop_back();
      }
      require_breaks(1);
    } else if (tag == "li") {
      if (!lists_.empty()) close_open_item(lists_.back());
      require_breaks(1);
    } else if (tag == "pre" || tag == "listing" || tag == "xmp") {
      if (pre_depth_ > 0) --pre_depth_;
      skip_lf_after_pre_ = false;
      require_breaks(1);
    } else if (tag == "tr") {
      require_breaks(1);
    } else if (is_paragraph_like(tag)) {
      require_breaks(indent_stack_.empty() ? 2 : 1);
    } else if (is_block_element(tag)) {
      require_breaks(1);
    }
  }

  std::string out_;
  int required_breaks_ = 0;
  bool pending_space_ = false;
  bool at_line_start_ = true;
  std::vector<ListFrame> lists_;
  std::vector<size_t> indent_stack_;  // content-column width of each open <li>
  std::string pending_marker_;        // the open <li>'s marker, until its first text
  int pre_depth_ = 0;
  bool skip_lf_after_pre_ = false;
  int cell_index_ = 0;
};

// ---------------------------------------------------------------------------
// RTF
// ---------------------------------------------------------------------------

// Windows-1252 0x80-0x9F; 0 = undefined. 0xA0-0xFF match Latin-1.
constexpr std::array<uint16_t, 32> kCp1252High{{
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
}};

uint32_t cp1252_to_unicode(unsigned char b) {
  if (b >= 0x80 && b <= 0x9F) {
    uint16_t cp = kCp1252High[static_cast<size_t>(b - 0x80)];
    return cp == 0 ? 0xFFFD : cp;
  }
  return b;
}

bool is_skipped_rtf_destination(std::string_view word) {
  static constexpr std::array<std::string_view, 40> kSkipped{
      "fonttbl",    "colortbl",     "stylesheet",        "info",       "pict",
      "object",     "header",       "headerl",           "headerr",    "headerf",
      "footer",     "footerl",      "footerr",           "footerf",    "footnote",
      "fldinst",    "listtable",    "listoverridetable", "revtbl",     "rsidtbl",
      "generator",  "xmlnstbl",     "themedata",         "colorschememapping",
      "datastore",  "latentstyles", "pgdsctbl",          "filetbl",    "expandedcolortbl",
      "mmathPr",    "author",       "operator",          "title",      "subject",
      "keywords",   "comment",      "doccomm",           "nonshppict", "bkmkstart",
      "bkmkend"};
  return std::find(kSkipped.begin(), kSkipped.end(), word) != kSkipped.end();
}

class RtfReader {
 public:
  std::string run(std::string_view rtf) {
    size_t i = 0;
    while (i < rtf.size()) {
      char c = rtf[i];
      if (c == '{') {
        stack_.push_back(cur_);
        skip_fallback_ = 0;
        ++i;
      } else if (c == '}') {
        bool was_label = cur_.label;
        if (!stack_.empty()) {
          cur_ = stack_.back();
          stack_.pop_back();
        }
        if (was_label && !cur_.label) finish_label();
        skip_fallback_ = 0;
        ++i;
      } else if (c == '\\') {
        i = control(rtf, i + 1);
      } else if (c == '\r' || c == '\n') {
        ++i;  // raw line breaks in RTF source are not content
      } else {
        text(std::string_view(&rtf[i], 1));
        ++i;
      }
    }
    return tidy_converted_text(out_);
  }

 private:
  struct GroupState {
    bool skip = false;   // inside a destination that isn't document text
    int uc = 1;          // fallback chars that follow each \uN
    bool label = false;  // inside {\listtext ...} / {\pntext ...}
  };

  // Every text-producing token goes through here; returns false when it
  // was swallowed as a \uN fallback character.
  bool consume_fallback() {
    if (skip_fallback_ > 0) {
      --skip_fallback_;
      return true;
    }
    return false;
  }

  void emit(std::string_view s) {
    if (cur_.skip) return;
    (cur_.label ? label_ : out_) += s;
  }

  void text(std::string_view s) {
    if (consume_fallback()) return;
    emit(s);
  }

  void emit_code_point(uint32_t cp) {
    std::string s;
    append_utf8(s, cp);
    emit(s);
  }

  void finish_label() {
    std::string_view l = label_;
    size_t first = l.find_first_not_of(" \t");
    if (first != std::string_view::npos) {
      size_t last = l.find_last_not_of(" \t");
      out_ += l.substr(first, last - first + 1);
      out_ += ' ';
    }
    label_.clear();
  }

  // Parses the control sequence after a backslash at rtf[i]; returns the
  // index just past it.
  size_t control(std::string_view rtf, size_t i) {
    if (i >= rtf.size()) return i;
    char c = rtf[i];
    if (c == '\\' || c == '{' || c == '}') {
      text(std::string_view(&rtf[i], 1));
      return i + 1;
    }
    if (c == '\'') {
      if (i + 1 < rtf.size()) {
        unsigned value = 0;
        auto [ptr, ec] = std::from_chars(rtf.data() + i + 1, rtf.data() + std::min(i + 3, rtf.size()),
                                         value, 16);
        size_t end = static_cast<size_t>(ptr - rtf.data());
        if (ec == std::errc()) {
          if (!consume_fallback()) emit_code_point(cp1252_to_unicode(static_cast<unsigned char>(value)));
          return end;
        }
      }
      return i + 1;
    }
    if (c == '*') {
      cur_.skip = true;  // "ignorable destination": nothing zedit needs lives there
      return i + 1;
    }
    if (c == '\n' || c == '\r') {
      text("\n");  // backslash-newline is a \par (Cocoa writes these)
      return i + 1;
    }
    if (c == '~') {
      text(" ");
      return i + 1;
    }
    if (c == '_') {
      text("-");
      return i + 1;
    }
    if (!is_alpha(c)) return i + 1;  // \- \: \| and friends: nothing to show

    size_t word_start = i;
    while (i < rtf.size() && is_alpha(rtf[i])) ++i;
    std::string_view word = rtf.substr(word_start, i - word_start);
    std::optional<long> param;
    size_t num_start = i;
    if (i < rtf.size() && (rtf[i] == '-' || is_digit(rtf[i]))) {
      long n = 0;
      auto [ptr, ec] = std::from_chars(rtf.data() + i, rtf.data() + rtf.size(), n);
      if (ec == std::errc()) {
        param = n;
        i = static_cast<size_t>(ptr - rtf.data());
      } else {
        i = num_start;
      }
    }
    if (i < rtf.size() && rtf[i] == ' ') ++i;  // the delimiter space is part of the word
    return control_word(word, param, rtf, i);
  }

  size_t control_word(std::string_view word, std::optional<long> param, std::string_view rtf,
                      size_t i) {
    if (word == "bin") {
      // Raw binary payload: skip it outright.
      size_t n = static_cast<size_t>(std::max(0L, param.value_or(0)));
      return std::min(rtf.size(), i + n);
    }
    if (word == "uc") {
      cur_.uc = static_cast<int>(std::max(0L, param.value_or(1)));
      return i;
    }
    if (word == "u") {
      if (consume_fallback()) return i;
      long n = param.value_or(0);
      if (n < 0) n += 65536;
      uint32_t cp = static_cast<uint32_t>(n);
      if (cp >= 0xD800 && cp <= 0xDBFF) {
        high_surrogate_ = cp;
      } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
        if (high_surrogate_ != 0) {
          emit_code_point(0x10000 + ((high_surrogate_ - 0xD800) << 10) + (cp - 0xDC00));
        }
        high_surrogate_ = 0;
      } else {
        high_surrogate_ = 0;
        emit_code_point(cp);
      }
      skip_fallback_ = cur_.uc;
      return i;
    }
    if (is_skipped_rtf_destination(word)) {
      cur_.skip = true;
      return i;
    }
    if (word == "listtext" || word == "pntext") {
      cur_.label = true;
      label_.clear();
      return i;
    }

    const char* out = nullptr;
    if (word == "par" || word == "line" || word == "sect" || word == "page" || word == "row") {
      out = "\n";
    } else if (word == "tab" || word == "cell") {
      out = "\t";
    } else if (word == "emdash") {
      out = "\xE2\x80\x94";
    } else if (word == "endash") {
      out = "\xE2\x80\x93";
    } else if (word == "bullet") {
      out = "\xE2\x80\xA2";
    } else if (word == "lquote") {
      out = "\xE2\x80\x98";
    } else if (word == "rquote") {
      out = "\xE2\x80\x99";
    } else if (word == "ldblquote") {
      out = "\xE2\x80\x9C";
    } else if (word == "rdblquote") {
      out = "\xE2\x80\x9D";
    } else if (word == "emspace" || word == "enspace" || word == "qmspace") {
      out = " ";
    }
    if (out != nullptr) {
      text(out);
    } else {
      consume_fallback();  // any other control word still counts as one fallback char
    }
    return i;
  }

  std::string out_;
  std::string label_;
  GroupState cur_;
  std::vector<GroupState> stack_;
  int skip_fallback_ = 0;
  uint32_t high_surrogate_ = 0;
};

}  // namespace

std::string html_to_plain_text(std::string_view html) { return HtmlRenderer{}.run(html); }

std::string rtf_to_plain_text(std::string_view rtf) { return RtfReader{}.run(rtf); }

std::string uri_list_to_plain_text(std::string_view uri_list) {
  std::string normalized = normalize_newlines(uri_list);
  std::string out;
  size_t start = 0;
  while (start < normalized.size()) {
    size_t nl = normalized.find('\n', start);
    size_t end = nl == std::string::npos ? normalized.size() : nl;
    std::string_view line = std::string_view(normalized).substr(start, end - start);
    while (!line.empty() && is_html_space(line.back())) line.remove_suffix(1);
    while (!line.empty() && is_html_space(line.front())) line.remove_prefix(1);
    if (!line.empty() && line.front() != '#') {
      if (!out.empty()) out += '\n';
      out += line;
    }
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  return out;
}

std::optional<std::string> clipboard_plain_text(const ClipboardFlavors& flavors) {
  if (flavors.plain_text && !flavors.plain_text->empty()) {
    return normalize_newlines(*flavors.plain_text);
  }
  if (flavors.html) {
    std::string text = html_to_plain_text(normalize_newlines(*flavors.html));
    if (!text.empty()) return text;
  }
  if (flavors.rtf) {
    std::string text = rtf_to_plain_text(*flavors.rtf);
    if (!text.empty()) return normalize_newlines(text);
  }
  if (flavors.uri_list) {
    std::string text = uri_list_to_plain_text(*flavors.uri_list);
    if (!text.empty()) return text;
  }
  return std::nullopt;
}

bool clipboard_has_text(const ClipboardFlavors& flavors) {
  return clipboard_plain_text(flavors).has_value();
}

}  // namespace zedit::core
