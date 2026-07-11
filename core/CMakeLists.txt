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
  src/languages.cpp
  src/diff.cpp
)

target_include_directories(zedit_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(zedit_core PRIVATE zedit_warnings)
target_link_libraries(zedit_core PUBLIC tree-sitter)
target_link_libraries(zedit_core PRIVATE tree-sitter-cpp tree-sitter-python tree-sitter-markdown)
target_compile_features(zedit_core PUBLIC cxx_std_20)
