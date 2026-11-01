add_library(zedit_warnings INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(zedit_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    # `Struct{first}` with default member initializers for the rest is an
    # intended idiom here (e.g. ExCommand{ExCommandKind::Quit}).
    -Wno-missing-field-initializers
  )
  # Clang 20+ flags __COUNTER__ (used by every Catch2 TEST_CASE) as a C2y
  # extension under -Wpedantic.
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-Wc2y-extensions ZEDIT_HAS_WC2Y_EXTENSIONS)
  if(ZEDIT_HAS_WC2Y_EXTENSIONS)
    target_compile_options(zedit_warnings INTERFACE -Wno-c2y-extensions)
  endif()
  # CI configures with -DZEDIT_WERROR=ON so new warnings fail the build;
  # off by default so a newer local compiler's new warnings don't block
  # anyone building from source.
  if(ZEDIT_WERROR)
    target_compile_options(zedit_warnings INTERFACE -Werror)
  endif()
endif()
