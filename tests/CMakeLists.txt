add_executable(zedit_tests
  core/test_version.cpp
  core/test_piece_table.cpp
  core/test_command_line.cpp
  core/test_editor.cpp
  core/test_motion.cpp
  core/test_operator.cpp
  core/test_normal_mode_operators.cpp
  core/test_visual_mode.cpp
  core/test_search.cpp
  core/test_search_mode.cpp
  core/test_named_registers.cpp
  core/test_multi_buffer.cpp
  core/test_dot_repeat.cpp
  core/test_highlight.cpp
  core/test_treesitter_smoke.cpp
  core/test_treesitter_highlighter.cpp
  core/test_more_languages.cpp
)

target_link_libraries(zedit_tests PRIVATE
  zedit_core
  Catch2::Catch2WithMain
  zedit_warnings
)

include(CTest)
include(Catch)
catch_discover_tests(zedit_tests)
