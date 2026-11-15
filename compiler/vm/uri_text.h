// The top-level functions that write text for a URL and read it back: escape and unescape (ECMAScript 3,
// Annex B), and encodeURI, encodeURIComponent, decodeURI and decodeURIComponent (15.1.3). Text is AS3's,
// UTF-8 indexed by UTF-16 unit (vm/text_units.h).
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace kas3 {
namespace vm {

// Every unit but letters, digits and @*_+-./ as %XX, or %uXXXX past U+00FF.
std::string escape_text(std::string_view p_text);
// %XX and %uXXXX back to their units; a % that begins neither is itself.
std::string unescape_text(std::string_view p_text);
// The text as UTF-8 bytes, each %XX but those of the unreserved set (and, for a whole URI, the reserved
// set and #). Nothing for a half of a character that is alone, which UTF-8 can't write.
std::optional<std::string> encode_uri(std::string_view p_text, bool p_component);
// The %XX sequences back to characters, those of the reserved set and # left as they are for a whole
// URI. Nothing for a % not followed by two hex digits, or bytes that are not UTF-8.
std::optional<std::string> decode_uri(std::string_view p_text, bool p_component);
// AS3's URIError for one of these functions: Error #1052.
std::string uri_error(std::string_view p_function);

} // namespace vm
} // namespace kas3
