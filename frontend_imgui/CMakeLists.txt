include(${CMAKE_SOURCE_DIR}/cmake/EmbedBinary.cmake)
set(ZEDIT_ICON_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/zedit_icon.hpp)
zedit_embed_binary(
  ${CMAKE_CURRENT_SOURCE_DIR}/../assets/logo/zedit-128.png
  ${ZEDIT_ICON_HEADER}
  kZeditIconPng
)

add_executable(zedit
  src/main.cpp
  src/app.cpp
  src/text_view.cpp
  src/status_line.cpp
  src/input_map.cpp
  src/font_manager.cpp
  src/theme.cpp
  src/menu_bar.cpp
  src/file_dialog.cpp
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
