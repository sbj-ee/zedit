include(FetchContent)

# Vendored deps build as static libraries for a simple, self-contained
# binary. Set globally (before any FetchContent_MakeAvailable) since
# tree-sitter's own CMakeLists only *offers* BUILD_SHARED_LIBS as an
# option -- option() is a no-op once the cache variable already exists.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ---- GLFW ----------------------------------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

# ---- Dear ImGui ------------------------------------------------------------
# Upstream ships no CMakeLists.txt, so we hand-declare a target compiling the
# core sources plus the GLFW + OpenGL3 backends.
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.9b
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)
find_package(OpenGL REQUIRED)
target_link_libraries(imgui PUBLIC OpenGL::GL)

# ---- tree-sitter core -----------------------------------------------------
# Ships its own working CMakeLists.txt at the repo root (lib/CMakeLists.txt
# in older releases; moved to the root as of the 0.25/0.26 restructuring),
# which already exposes lib/include PUBLICly -- no hand-written glue needed.
# Pinned to a release supporting ABI 15 (TREE_SITTER_LANGUAGE_VERSION),
# since tree-sitter-markdown's checked-in parser.c requires it; ABI 13-14
# grammars (cpp, python) remain compatible under the same range.
FetchContent_Declare(
  tree_sitter
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
  GIT_TAG v0.26.10
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tree_sitter)

# ---- tree-sitter grammars --------------------------------------------------
include(${CMAKE_SOURCE_DIR}/cmake/TreeSitterGrammar.cmake)

# C++'s own highlights.scm only covers C++-specific constructs (templates,
# class/namespace keywords, ...); it's designed to be layered on top of C's
# query for the constructs they share (comments, strings, numbers,
# operators, ...), same as nvim-treesitter's "; inherits: c" convention.
zedit_fetch_query_only(
  tree-sitter-c-queries
  https://github.com/tree-sitter/tree-sitter-c.git
  v0.23.4
)
zedit_add_tree_sitter_grammar(
  tree-sitter-cpp
  https://github.com/tree-sitter/tree-sitter-cpp.git
  v0.23.4
  tree_sitter_cpp
  kCppHighlightsQuery
  INHERIT_QUERY_DIRS ${tree-sitter-c-queries_SOURCE_DIR}/queries
)

# C's own grammar, compiled directly for .c files -- a second fetch of the
# same repo/tag as tree-sitter-c-queries above (that one's SOURCE_DIR
# isn't visible outside zedit_fetch_query_only's own scope, since it's
# the only one of these two helper functions that propagates it via
# PARENT_SCOPE), which costs one extra shallow clone of a small repo but
# keeps this addition from having to touch either working, tested helper.
zedit_add_tree_sitter_grammar(
  tree-sitter-c
  https://github.com/tree-sitter/tree-sitter-c.git
  v0.23.4
  tree_sitter_c
  kCHighlightsQuery
)

zedit_add_tree_sitter_grammar(
  tree-sitter-python
  https://github.com/tree-sitter/tree-sitter-python.git
  v0.23.6
  tree_sitter_python
  kPythonHighlightsQuery
)

# Markdown ships as two grammars (block structure + a separate "inline"
# grammar for emphasis/links/inline-code within paragraph text) combined
# via tree-sitter's language-injection mechanism -- both are wired up
# (MarkdownHighlighter in core/ runs both and merges their spans; a plain
# TreeSitterHighlighter can only run one grammar at a time).
zedit_add_tree_sitter_grammar(
  tree-sitter-markdown
  https://github.com/tree-sitter-grammars/tree-sitter-markdown.git
  v0.5.3
  tree_sitter_markdown
  kMarkdownHighlightsQuery
  REPO_SUBDIR tree-sitter-markdown
)
zedit_add_tree_sitter_grammar(
  tree-sitter-markdown-inline
  https://github.com/tree-sitter-grammars/tree-sitter-markdown.git
  v0.5.3
  tree_sitter_markdown_inline
  kMarkdownInlineHighlightsQuery
  REPO_SUBDIR tree-sitter-markdown-inline
)

# ---- nlohmann/json ---------------------------------------------------------
# Header-only; used for LSP JSON-RPC message encoding/decoding.
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ---- Lua 5.4 ---------------------------------------------------------------
# Upstream ships a plain Makefile, not a CMakeLists.txt, so (same approach as
# Dear ImGui above) we hand-declare a target compiling its C sources
# directly. Excludes lua.c/luac.c (the standalone `lua`/`luac` CLI mains) --
# we only want the embeddable library. No platform defines (LUA_USE_LINUX
# etc.) are set: those only enable optional dynamic-C-module loading via
# require(), which zedit's config scripts never need.
FetchContent_Declare(
  lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG v5.4.7
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lua)

file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
# lua.c/luac.c are the standalone CLI mains (excluded above); onelua.c is an
# alternate all-in-one build that #includes every other .c file itself, and
# ltests.c is an internal debug/test module -- compiling either alongside
# the normal per-file sources means duplicate global symbol definitions in
# the same static library. That happened to still link here (the linker
# never needed onelua.c.o's copies, so they just sat unused in the
# archive), but relying on that is fragile across linkers/platforms, so
# they're excluded explicitly rather than left to chance.
list(REMOVE_ITEM LUA_SOURCES
  ${lua_SOURCE_DIR}/lua.c
  ${lua_SOURCE_DIR}/luac.c
  ${lua_SOURCE_DIR}/onelua.c
  ${lua_SOURCE_DIR}/ltests.c
)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${lua_SOURCE_DIR})
if(UNIX)
  target_link_libraries(lua PRIVATE m)
endif()

# ---- stb_image (single header) --------------------------------------------
# Used only to load the window icon PNG at startup -- Dear ImGui's own font
# atlas uses its bundled stb_truetype for TTF glyphs, not this, so this is
# the only place PNG decoding is needed. A single header, so it's just
# downloaded directly rather than declared as a git FetchContent dependency.
set(STB_IMAGE_DIR ${CMAKE_BINARY_DIR}/_deps/stb_image-src)
file(MAKE_DIRECTORY ${STB_IMAGE_DIR})
file(DOWNLOAD
  https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
  ${STB_IMAGE_DIR}/stb_image.h
)
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${STB_IMAGE_DIR})

# ---- Catch2 (tests only) --------------------------------------------------
if(ZEDIT_BUILD_TESTS)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
