#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include "zedit/core/languages.hpp"
#include "zedit/core/markdown_highlighter.hpp"
#include "zedit/core/treesitter_highlighter.hpp"

using zedit::core::HighlightSpan;
using zedit::core::make_highlighter_for_filename;

namespace {

std::vector<std::tuple<size_t, size_t, int>> as_tuples(const std::vector<HighlightSpan>& spans) {
  std::vector<std::tuple<size_t, size_t, int>> out;
  for (const auto& s : spans) out.emplace_back(s.start_byte, s.end_byte, static_cast<int>(s.kind));
  std::sort(out.begin(), out.end());
  return out;
}

template <typename H>
bool incremental(const zedit::core::Highlighter& h) {
  const auto* typed = dynamic_cast<const H*>(&h);
  REQUIRE(typed != nullptr);
  return typed->last_parse_was_incremental();
}

// Applies random small edits; after each the incremental highlighter must
// give exactly the spans a fresh full parse gives.
template <typename H>
void check_random_edits(const char* filename, std::string text, const char* alphabet) {
  auto live = make_highlighter_for_filename(filename);
  live->set_text(text);
  REQUIRE_FALSE(incremental<H>(*live));  // first parse is a full one
  std::mt19937 rng(7);
  std::string chars(alphabet);
  for (int i = 0; i < 200; ++i) {
    size_t pos = std::uniform_int_distribution<size_t>(0, text.size())(rng);
    if (!text.empty() && rng() % 3 == 0) {
      size_t len = std::min<size_t>(text.size() - std::min(pos, text.size() - 1),
                                    1 + rng() % 4);
      pos = std::min(pos, text.size() - 1);
      text.erase(pos, len);
    } else {
      std::string ins;
      for (auto n = static_cast<unsigned>(1 + rng() % 3); n > 0; --n) ins += chars[rng() % chars.size()];
      text.insert(pos, ins);
    }
    live->set_text(text);
    REQUIRE(incremental<H>(*live));
    auto fresh = make_highlighter_for_filename(filename);
    fresh->set_text(text);
    INFO(text);
    REQUIRE(as_tuples(live->highlight(0, text.size())) ==
            as_tuples(fresh->highlight(0, text.size())));
  }
}

}  // namespace

TEST_CASE("C++ highlighting stays exact across incremental reparses", "[treesitter][incremental]") {
  check_random_edits<zedit::core::TreeSitterHighlighter>(
      "a.cpp",
      "// comment\n#include <x>\nint main() {\n  const char* s = \"hi\";\n  return 0;\n}\n",
      "ab1 ;{}()\"/*\n#");
}

TEST_CASE("Markdown highlighting stays exact across incremental reparses",
          "[treesitter][incremental]") {
  check_random_edits<zedit::core::MarkdownHighlighter>(
      "a.md", "# Title\n\nSome *em* and `code`.\n\n```\nblock\n```\n\n- item\n", "ab #*`-\n");
}

TEST_CASE("unchanged text skips the reparse entirely", "[treesitter][incremental]") {
  auto h = make_highlighter_for_filename("a.cpp");
  std::string code = "int x = 1;\n";
  h->set_text(code);
  auto before = as_tuples(h->highlight(0, code.size()));
  h->set_text(code);
  REQUIRE(incremental<zedit::core::TreeSitterHighlighter>(*h));
  REQUIRE(as_tuples(h->highlight(0, code.size())) == before);
}
