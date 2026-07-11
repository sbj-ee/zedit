add_executable(zedit
  src/main.cpp
  src/app.cpp
  src/text_view.cpp
  src/status_line.cpp
  src/input_map.cpp
  src/font_manager.cpp
)

target_link_libraries(zedit PRIVATE
  zedit_core
  imgui
  zedit_warnings
)
