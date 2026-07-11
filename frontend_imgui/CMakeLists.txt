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
)

target_include_directories(zedit PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(zedit PRIVATE
  zedit_core
  imgui
  stb_image
  zedit_warnings
)

set_target_properties(zedit PROPERTIES OUTPUT_NAME ze)
