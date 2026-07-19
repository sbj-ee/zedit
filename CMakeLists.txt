cmake_minimum_required(VERSION 3.21)

# zedit only supports Apple Silicon on macOS. This must be set before
# project() -- setting CMAKE_OSX_ARCHITECTURES any later is too late for the
# compiler checks project() runs. Guarded so an explicit
# -DCMAKE_OSX_ARCHITECTURES=... on the command line still wins. Without this,
# CMake defaults to whatever arch the running cmake/ninja binaries are built
# for -- on this repo's dev machine those are x86_64 Homebrew binaries
# running under Rosetta on an arm64 host, which silently produced x86_64
# builds despite the host being Apple Silicon.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for macOS" FORCE)
endif()

# C is needed alongside C++ because tree-sitter grammar sources (parser.c,
# scanner.c) are compiled directly as part of this project (see
# cmake/TreeSitterGrammar.cmake), not via a subproject with its own C-only
# project() call.
# VERSION is kept in sync with core/include/zedit/core/version.hpp by
# hand (no generation step) -- update both together on a version bump.
project(zedit VERSION 1.6.1 LANGUAGES CXX C)

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
