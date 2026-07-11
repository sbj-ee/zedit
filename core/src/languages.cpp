#include "zedit/core/languages.hpp"

#include "zedit/core/markdown_highlighter.hpp"
#include "zedit/core/treesitter_highlighter.hpp"

extern "C" const TSLanguage* tree_sitter_c(void);
extern "C" const TSLanguage* tree_sitter_cpp(void);
extern "C" const TSLanguage* tree_sitter_python(void);
extern "C" const TSLanguage* tree_sitter_markdown(void);
extern "C" const TSLanguage* tree_sitter_markdown_inline(void);
extern const char* const kCHighlightsQuery;
extern const char* const kCppHighlightsQuery;
extern const char* const kPythonHighlightsQuery;
extern const char* const kMarkdownHighlightsQuery;
extern const char* const kMarkdownInlineHighlightsQuery;

namespace zedit::core {

namespace {

bool has_extension(std::string_view filename, std::string_view ext) {
  return filename.size() >= ext.size() &&
         filename.substr(filename.size() - ext.size()) == ext;
}

bool has_any_extension(std::string_view filename,
                        std::initializer_list<std::string_view> extensions) {
  for (std::string_view ext : extensions) {
    if (has_extension(filename, ext)) return true;
  }
  return false;
}

}  // namespace

std::unique_ptr<Highlighter> make_highlighter_for_filename(std::string_view filename) {
  // Checked before is_cpp_filename(): that function's own extension list
  // (.cpp/.cc/.cxx/.hpp/.h/.hh) doesn't include ".c", so there's no
  // overlap to worry about here -- a genuine ".c" file gets its own
  // grammar instead of (mis)using C++'s, which doesn't parse every valid
  // C construct (e.g. C99 designated initializers) correctly.
  if (has_any_extension(filename, {".c"})) {
    return std::make_unique<TreeSitterHighlighter>(tree_sitter_c(), kCHighlightsQuery);
  }
  if (is_cpp_filename(filename)) {
    return std::make_unique<TreeSitterHighlighter>(tree_sitter_cpp(), kCppHighlightsQuery);
  }
  if (has_any_extension(filename, {".py", ".pyw"})) {
    return std::make_unique<TreeSitterHighlighter>(tree_sitter_python(), kPythonHighlightsQuery);
  }
  if (has_any_extension(filename, {".md", ".markdown"})) {
    return std::make_unique<MarkdownHighlighter>(tree_sitter_markdown(), kMarkdownHighlightsQuery,
                                                   tree_sitter_markdown_inline(),
                                                   kMarkdownInlineHighlightsQuery);
  }
  return std::make_unique<PlainHighlighter>();
}

bool is_cpp_filename(std::string_view filename) {
  return has_any_extension(filename, {".cpp", ".cc", ".cxx", ".hpp", ".h", ".hh"});
}

}  // namespace zedit::core
