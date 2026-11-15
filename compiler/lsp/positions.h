// Places in a line as the protocol counts them: UTF-16 code units, or bytes of UTF-8.
#pragma once

#include <cstdint>
#include <string_view>

namespace kas3 {

// UTF-16 code units in a stretch of UTF-8.
int utf16_units(std::string_view p_text);
// The bytes that hold the first p_units UTF-16 code units of the line. A position inside a character
// counts the whole character.
uint32_t bytes_for_units(std::string_view p_line, int64_t p_units);

} // namespace kas3
