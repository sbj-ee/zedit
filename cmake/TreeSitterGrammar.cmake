# Fetches a tree-sitter grammar repo and compiles its parser.c (+scanner.c
# if present) directly into a static library, bypassing the repo's own
# CMakeLists.txt. Those CMakeLists declare a custom_command that
# regenerates parser.c via the `tree-sitter` CLI whenever grammar.json looks
# newer -- which git checkouts don't reliably order, and the CLI isn't a
# dependency we want to require. parser.c is already checked into these
# repos, so we just compile it as-is, the same way FetchDeps.cmake already
# hand-builds Dear ImGui.
#
# Also embeds queries/highlights.scm as a C++ string constant (read at
# configure time) so the running editor never needs to locate that file on
# disk -- it ships baked into the binary. Grammars whose own highlights.scm
# only covers language-specific additions (e.g. C++'s doesn't define
# comments/strings/numbers -- it relies on being combined with C's, the way
# nvim-treesitter's "; inherits: c" convention works) can list
# INHERIT_QUERY_DIRS of other repos' queries/ directories to prepend.
#
# Some grammar repos are monorepos with the actual grammar in a
# subdirectory (e.g. tree-sitter-markdown's block grammar lives under
# tree-sitter-markdown/ alongside a separate -inline/ grammar) -- pass
# REPO_SUBDIR to point at it.
function(zedit_add_tree_sitter_grammar name repo tag lang_symbol query_var_name)
  cmake_parse_arguments(ARG "" "REPO_SUBDIR" "INHERIT_QUERY_DIRS" ${ARGN})

  FetchContent_Declare(${name} GIT_REPOSITORY ${repo} GIT_TAG ${tag} GIT_SHALLOW TRUE)
  FetchContent_GetProperties(${name})
  if(NOT ${name}_POPULATED)
    FetchContent_Populate(${name})
  endif()
  set(src_dir ${${name}_SOURCE_DIR})
  if(ARG_REPO_SUBDIR)
    set(src_dir ${src_dir}/${ARG_REPO_SUBDIR})
  endif()

  add_library(${name} STATIC ${src_dir}/src/parser.c)
  if(EXISTS ${src_dir}/src/scanner.c)
    target_sources(${name} PRIVATE ${src_dir}/src/scanner.c)
  endif()
  target_include_directories(${name} PRIVATE ${src_dir}/src)
  set_target_properties(${name} PROPERTIES C_STANDARD 11)

  set(query_contents "")
  foreach(dir ${ARG_INHERIT_QUERY_DIRS})
    file(READ ${dir}/highlights.scm inherited_query)
    string(APPEND query_contents "${inherited_query}\n")
  endforeach()
  file(READ ${src_dir}/queries/highlights.scm own_query)
  string(APPEND query_contents "${own_query}")

  set(generated_cpp ${CMAKE_CURRENT_BINARY_DIR}/${name}_query.cpp)
  file(WRITE ${generated_cpp}
    "extern const char* const ${query_var_name} = R\"ZEDIT_QUERY(${query_contents})ZEDIT_QUERY\";\n")
  target_sources(${name} PRIVATE ${generated_cpp})

  message(STATUS "tree-sitter grammar ${name}: language symbol ${lang_symbol}, query var ${query_var_name}")
endfunction()

# Populates a grammar repo's source (for its queries/) without compiling
# it -- used for repos we only want as a source of an inherited query, not
# as a parser we'll actually invoke.
function(zedit_fetch_query_only name repo tag)
  FetchContent_Declare(${name} GIT_REPOSITORY ${repo} GIT_TAG ${tag} GIT_SHALLOW TRUE)
  FetchContent_GetProperties(${name})
  if(NOT ${name}_POPULATED)
    FetchContent_Populate(${name})
  endif()
  set(${name}_SOURCE_DIR ${${name}_SOURCE_DIR} PARENT_SCOPE)
endfunction()
