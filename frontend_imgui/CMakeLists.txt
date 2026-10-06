include(${CMAKE_SOURCE_DIR}/cmake/EmbedBinary.cmake)
set(ZEDIT_ICON_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/zedit_icon.hpp)
zedit_embed_binary(
  ${CMAKE_CURRENT_SOURCE_DIR}/../assets/logo/zedit-128.png
  ${ZEDIT_ICON_HEADER}
  kZeditIconPng
)

# macOS clipboard reads go through NSPasteboard instead of GLFW, whose
# glfwGetClipboardString() only accepts plain text and returns NULL for an
# HTML-only or RTF-only copy (so paste silently did nothing). Its own small
# library so the test suite can exercise it against a real pasteboard.
if(APPLE)
  add_library(zedit_macos_pasteboard STATIC src/macos_pasteboard.mm)
  set_source_files_properties(src/macos_pasteboard.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_include_directories(zedit_macos_pasteboard PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(zedit_macos_pasteboard
    PUBLIC zedit_core
    PRIVATE zedit_warnings "-framework AppKit"
  )
endif()

# Linux clipboard reads go through zedit's own X11 CLIPBOARD reader (or
# wl-paste on Wayland) instead of only glfwGetClipboardString(), which asks
# for nothing but plain text and returned NULL for an HTML-only or RTF-only
# copy. See src/linux_clipboard.hpp. Its own small library so the test
# suite can exercise it against real X11/Wayland clipboards.
if(UNIX AND NOT APPLE)
  add_library(zedit_linux_clipboard STATIC
    src/linux_clipboard.cpp
    src/wl_paste_clipboard.cpp
  )
  target_include_directories(zedit_linux_clipboard PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(zedit_linux_clipboard
    PUBLIC zedit_core glfw
    PRIVATE zedit_warnings
  )
  # Only Xlib's *headers* are needed: like GLFW 3.4, the reader dlopen()s
  # libX11.so.6 at runtime, so the binary (and the .deb) gain no hard
  # libX11 dependency. GLFW's X11 backend already requires the same
  # headers (libx11-dev), so this adds no build dependency either.
  if(GLFW_BUILD_X11)
    find_package(X11 REQUIRED)
    target_sources(zedit_linux_clipboard PRIVATE src/x11_clipboard.cpp)
    target_include_directories(zedit_linux_clipboard PRIVATE ${X11_INCLUDE_DIR})
    target_compile_definitions(zedit_linux_clipboard PUBLIC ZEDIT_HAVE_X11_CLIPBOARD)
    target_link_libraries(zedit_linux_clipboard PRIVATE ${CMAKE_DL_LIBS})
  endif()
endif()

add_executable(zedit
  src/main.cpp
  src/app.cpp
  src/text_view.cpp
  src/status_line.cpp
  src/input_map.cpp
  src/font_manager.cpp
  src/theme.cpp
  src/menu_bar.cpp
  src/command_palette.cpp
  src/file_dialog.cpp
  src/file_finder.cpp
  src/find_replace_dialog.cpp
  src/recent_files.cpp
  src/update_checker.cpp
)

target_include_directories(zedit PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(zedit PRIVATE
  zedit_core
  imgui
  stb_image
  zedit_warnings
)
if(APPLE)
  target_link_libraries(zedit PRIVATE zedit_macos_pasteboard)
elseif(UNIX)
  target_link_libraries(zedit PRIVATE zedit_linux_clipboard)
endif()

set_target_properties(zedit PROPERTIES OUTPUT_NAME ze)

if(APPLE)
  # Produces a real ze.app bundle (not just a bare Unix executable) so
  # cmake/Packaging.cmake's DragNDrop generator has something meaningful
  # to put in the .dmg -- MACOSX_BUNDLE_BUNDLE_NAME controls the
  # user-visible name in Finder/Dock independent of the bundle
  # directory's own name (still "ze.app", from OUTPUT_NAME above).
  set(ZEDIT_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/../assets/logo/zedit.icns)
  target_sources(zedit PRIVATE ${ZEDIT_ICNS})
  set_source_files_properties(${ZEDIT_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(zedit PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "zedit"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.sbj-ee.zedit"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "zedit.icns"
  )
endif()
