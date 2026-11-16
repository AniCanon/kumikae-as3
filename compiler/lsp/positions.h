// Places in a text as the protocol counts them: a line, and in it UTF-16 code units or bytes of UTF-8.
#pragma once

#include <cstdint>
#include <string_view>

namespace kas3 {

// The protocol's `character` for the first p_bytes bytes of a line.
int character_at(std::string_view p_line, size_t p_bytes, bool p_utf8);
// The bytes of a line that come before the protocol's `character`. One that falls inside a character
// counts the whole character, so that none is ever cut in two; one past the end is the end.
uint32_t bytes_before(std::string_view p_line, int64_t p_character, bool p_utf8);
// Where a line and character are in a text as an editor holds it: a byte-order mark, if the text has
// one, is the first character of the first line. "\n", "\r\n" and "\r" each end a line. A line past the
// last is the end of the text.
uint32_t offset_in_text(std::string_view p_text, int64_t p_line, int64_t p_character, bool p_utf8);

} // namespace kas3
