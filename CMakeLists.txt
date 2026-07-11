cmake_minimum_required(VERSION 3.21)
# C is needed alongside C++ because tree-sitter grammar sources (parser.c,
# scanner.c) are compiled directly as part of this project (see
# cmake/TreeSitterGrammar.cmake), not via a subproject with its own C-only
# project() call.
# VERSION is kept in sync with core/include/zedit/core/version.hpp by
# hand (no generation step) -- update both together on a version bump.
project(zedit VERSION 1.4.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(ZEDIT_BUILD_TESTS "Build the Catch2 test suite" ON)

include(cmake/CompilerWarnings.cmake)
include(cmake/FetchDeps.cmake)

add_subdirectory(core)
add_subdirectory(frontend_imgui)

if(ZEDIT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(cmake/Packaging.cmake)
