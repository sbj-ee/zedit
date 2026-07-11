add_executable(zedit_tests
  core/test_version.cpp
  core/test_piece_table.cpp
  core/test_command_line.cpp
  core/test_editor.cpp
)

target_link_libraries(zedit_tests PRIVATE
  zedit_core
  Catch2::Catch2WithMain
  zedit_warnings
)

include(CTest)
include(Catch)
catch_discover_tests(zedit_tests)
