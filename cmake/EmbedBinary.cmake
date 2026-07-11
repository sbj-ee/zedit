# Embeds a binary file as a C++ byte array header at configure time, same
# spirit as how TreeSitterGrammar.cmake embeds .scm query text -- avoids any
# runtime "where do I find my asset relative to the binary" path-resolution
# problem (see font_manager.cpp's system-font-path fallback list for what
# that problem looks like when it can't be sidestepped this way).
function(zedit_embed_binary input_file output_header variable_name)
  file(READ ${input_file} hex_content HEX)
  string(REGEX MATCHALL "([A-Za-z0-9][A-Za-z0-9])" hex_bytes ${hex_content})
  list(JOIN hex_bytes ",0x" byte_array)
  string(LENGTH "${hex_content}" hex_len)
  math(EXPR byte_len "${hex_len} / 2")

  file(WRITE ${output_header}
"#pragma once
#include <cstddef>
inline constexpr unsigned char ${variable_name}[] = {0x${byte_array}};
inline constexpr size_t ${variable_name}_len = ${byte_len};
")
endfunction()
