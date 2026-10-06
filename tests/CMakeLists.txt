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
  core/test_windows.cpp
  core/test_diff.cpp
  core/test_diff_command.cpp
  core/test_json_smoke.cpp
  core/test_json_rpc_framing.cpp
  core/test_subprocess.cpp
  core/test_json_rpc_transport.cpp
  core/test_lsp_clangd.cpp
  core/test_editor_lsp.cpp
  core/test_config.cpp
  core/test_config_integration.cpp
  core/test_ctrl_shortcuts.cpp
  core/test_line_navigation.cpp
  core/test_file_io.cpp
  core/test_auto_indent.cpp
  core/test_find_replace.cpp
  core/test_sort_lines.cpp
  core/test_bracket_match.cpp
  core/test_word_wrap.cpp
  core/test_update_check.cpp
  core/test_fuzzy_match.cpp
  core/test_git_diff.cpp
  core/test_gedit_style.cpp
  core/test_recovery.cpp
  core/test_system_clipboard.cpp
  core/test_clipboard_text.cpp
  core/test_clipboard_source.cpp
)

target_link_libraries(zedit_tests PRIVATE
  zedit_core
  Catch2::Catch2WithMain
  zedit_warnings
)

# Real-NSPasteboard round trips for the macOS clipboard reader (see
# frontend_imgui/src/macos_pasteboard.mm).
if(APPLE)
  target_sources(zedit_tests PRIVATE frontend/test_macos_pasteboard.cpp)
  target_link_libraries(zedit_tests PRIVATE zedit_macos_pasteboard)
endif()

# Linux clipboard readers (frontend_imgui/src/linux_clipboard.cpp): fake
# wl-paste scripts always; the real X11 CLIPBOARD and Wayland clipboard
# only with ZEDIT_TEST_REAL_CLIPBOARD=1 (see the test file).
if(UNIX AND NOT APPLE AND TARGET zedit_linux_clipboard AND GLFW_BUILD_X11)
  target_sources(zedit_tests PRIVATE frontend/test_linux_clipboard.cpp frontend/x11_test_owner.cpp)
  # The tests link libX11 directly (unlike zedit, which dlopen()s it) to
  # play an X11 clipboard owner that never answers.
  find_package(X11 REQUIRED)
  target_link_libraries(zedit_tests PRIVATE zedit_linux_clipboard X11::X11)

  # Wayland clipboard owner for the real-clipboard tests: offers exactly
  # the given MIME types via wlr-data-control (wl-copy can't make an
  # HTML-only clipboard; see tests/helpers/wl_clipboard_owner.cpp).
  # Test-only; built when wayland-client and wayland-scanner are present
  # (GLFW's Wayland backend needs both anyway).
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(ZEDIT_WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
  endif()
  find_program(ZEDIT_WAYLAND_SCANNER wayland-scanner)
  if(ZEDIT_WAYLAND_CLIENT_FOUND AND ZEDIT_WAYLAND_SCANNER)
    set(_wl_xml ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wlr-data-control-unstable-v1.xml)
    set(_wl_gen ${CMAKE_CURRENT_BINARY_DIR}/wayland-generated)
    file(MAKE_DIRECTORY ${_wl_gen})
    add_custom_command(
      OUTPUT ${_wl_gen}/wlr-data-control-unstable-v1-client-protocol.h
             ${_wl_gen}/wlr-data-control-unstable-v1-protocol.c
      COMMAND ${ZEDIT_WAYLAND_SCANNER} client-header ${_wl_xml}
              ${_wl_gen}/wlr-data-control-unstable-v1-client-protocol.h
      COMMAND ${ZEDIT_WAYLAND_SCANNER} private-code ${_wl_xml}
              ${_wl_gen}/wlr-data-control-unstable-v1-protocol.c
      DEPENDS ${_wl_xml}
      VERBATIM
    )
    add_executable(zedit_wl_clipboard_owner
      helpers/wl_clipboard_owner.cpp
      ${_wl_gen}/wlr-data-control-unstable-v1-client-protocol.h
      ${_wl_gen}/wlr-data-control-unstable-v1-protocol.c
    )
    target_include_directories(zedit_wl_clipboard_owner PRIVATE ${_wl_gen})
    target_link_libraries(zedit_wl_clipboard_owner PRIVATE PkgConfig::ZEDIT_WAYLAND_CLIENT)
    add_dependencies(zedit_tests zedit_wl_clipboard_owner)
    target_compile_definitions(zedit_tests PRIVATE
      ZEDIT_WL_OWNER_HELPER="$<TARGET_FILE:zedit_wl_clipboard_owner>")
  endif()
endif()

include(CTest)
include(Catch)
catch_discover_tests(zedit_tests)
