add_library(zedit_core STATIC
  src/version.cpp
  src/piece_table.cpp
  src/file_io.cpp
  src/command_line.cpp
  src/motion.cpp
  src/operator.cpp
  src/search.cpp
  src/mode_state_machine.cpp
  src/editor.cpp
  src/treesitter_highlighter.cpp
  src/markdown_highlighter.cpp
  src/languages.cpp
  src/diff.cpp
  src/subprocess.cpp
  src/json_rpc.cpp
  src/lsp.cpp
  src/config.cpp
  src/word_wrap.cpp
  src/update_check.cpp
  src/fuzzy_match.cpp
  src/git_diff.cpp
)

target_include_directories(zedit_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(zedit_core PRIVATE zedit_warnings)
target_link_libraries(zedit_core PUBLIC tree-sitter)
target_link_libraries(zedit_core PRIVATE tree-sitter-c tree-sitter-cpp tree-sitter-python tree-sitter-markdown tree-sitter-markdown-inline)
target_link_libraries(zedit_core PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(zedit_core PUBLIC lua)
find_package(Threads REQUIRED)
target_link_libraries(zedit_core PUBLIC Threads::Threads)
target_compile_features(zedit_core PUBLIC cxx_std_20)
