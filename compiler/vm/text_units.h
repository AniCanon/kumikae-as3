// AS3's strings are held as UTF-8 and indexed as AS3 indexes them: by UTF-16 unit.
//
// Text keeps one form, so that two equal strings have the same bytes. A character past U+FFFF is always
// its four bytes. A half of one with no partner beside it (from charAt, a cut, or fromCharCode) is held
// alone, as the three bytes UTF-8 would give that one unit; two such halves that come together are joined
// into the four bytes again.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kas3 {
namespace vm {

// The code point that starts at byte p_at, and how many bytes it takes. A half held alone is read as the
// unit it is. Text that is not well-formed is read a byte at a time.
uint32_t code_point_at(std::string_view p_text, size_t p_at, size_t *r_size);
// The text as UTF-16 units, and units as text again (two halves side by side become their character).
std::u16string units_of(std::string_view p_text);
std::string text_of_units(std::u16string_view p_units);
// Adds the UTF-8 of one code point (a half held alone included) as it is, joining nothing.
void encode(std::string *r_text, uint32_t p_code_point);
// The UTF-16 unit at p_index, if the text has one there.
std::optional<uint16_t> unit_at(std::string_view p_text, uint32_t p_index);
// Units p_start up to p_end of the text, which the caller has kept within it (start <= end <= length).
std::string units_between(std::string_view p_text, uint32_t p_start, uint32_t p_end);
// Adds the text after r_text, joining a half left alone at the end of r_text with its partner at the start
// of p_text.
void append_text(std::string *r_text, std::string_view p_text);
// Adds one UTF-16 unit, joined with the half before it if the two make a character.
void append_unit(std::string *r_text, uint16_t p_unit);
// How many bytes joining p_first and p_second saves: 2 if the first ends with a lone high half and the
// second starts with a lone low half (the two become one character of four bytes), otherwise 0.
size_t joined_halves(std::string_view p_first, std::string_view p_second);
// Writes the four bytes of the character that the halves at the end of p_first and the start of p_second
// make, when joined_halves() says they make one.
void write_joined(std::string_view p_first, std::string_view p_second, char *r_four);
// Where p_needle is first found in p_text at or after unit p_from, or last found at or before it, in
// units; -1 if it is not found there. An empty needle is found at p_from, or at the end if that is before.
int32_t find_units(std::string_view p_text, std::string_view p_needle, uint32_t p_from);
int32_t find_last_units(std::string_view p_text, std::string_view p_needle, uint32_t p_from);
// The text in capitals or in small letters, a character for a character, as AS3 changes case: by
// Unicode's mappings within the first 65,536 characters that give one character (vm/case_table.cpp).
// "ß" stays as it is, where ECMAScript today gives "SS".
std::string upper_case(std::string_view p_text);
std::string lower_case(std::string_view p_text);
// The other character of a pair of cases, or the character itself if it has none (vm/case_table.cpp).
uint32_t to_upper(uint32_t p_code_point);
uint32_t to_lower(uint32_t p_code_point);

} // namespace vm
} // namespace kas3
