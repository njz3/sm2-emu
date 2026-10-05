# Rewrites a binary file as a constexpr byte array in a header.
#   cmake -DSM2_IN=<file> -DSM2_OUT=<header> -DSM2_SYMBOL=<name> -P EmbedBinary.cmake
foreach(var SM2_IN SM2_OUT SM2_SYMBOL)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "EmbedBinary.cmake: ${var} is required")
    endif()
endforeach()
get_filename_component(source_name "${SM2_IN}" NAME)
file(READ "${SM2_IN}" hex HEX)
string(LENGTH "${hex}" hex_length)
math(EXPR byte_count "${hex_length} / 2")
if(byte_count EQUAL 0)
    message(FATAL_ERROR "EmbedBinary.cmake: ${SM2_IN} is empty")
endif()
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n    " bytes "${bytes}")
file(WRITE "${SM2_OUT}"
"// Generated from ${source_name} by EmbedBinary.cmake. Do not edit.
#pragma once

#include <cstddef>

namespace sm2::assets {

inline constexpr unsigned char ${SM2_SYMBOL}[] = {
    ${bytes}
};
inline constexpr std::size_t ${SM2_SYMBOL}_size = ${byte_count};

}  // namespace sm2::assets
")
