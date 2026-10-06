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
# Single source of truth for version: bump ONLY the VERSION here.
# core/ generates include/zedit/core/version.hpp from these numbers
# (About dialog, update check, tests). CPack and macOS bundle already
# use PROJECT_VERSION.
project(zedit VERSION 1.7.4 LANGUAGES CXX C)

# macOS reads the clipboard through NSPasteboard directly
# (frontend_imgui/src/macos_pasteboard.mm), so Objective-C++ is needed
# there. Enabled here at the top level because both the app and the test
# executable link it. OBJC is enabled alongside it so GLFW's Cocoa .m
# sources keep compiling as Objective-C: with only OBJCXX enabled, CMake
# compiles .m files as Objective-C++, which GLFW's C code doesn't build as.
if(APPLE)
  enable_language(OBJC OBJCXX)
endif()

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
