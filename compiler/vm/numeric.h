// Numbers as AS3 converts and compares them, where C++ alone would not say or would not be safe.
#pragma once

#include <cstdint>
#include <string_view>

namespace kas3 {
namespace vm {

// A Number as an Int64, the way a conversion the program wrote does it: not-a-number is 0, and a value
// past either end is held at that end.
int64_t clamp_wide(double p_number);
// Text as an Int64: digits are read exactly, so that every Int64 can be written; anything else is read
// as a Number first, as int("12.7") is.
int64_t string_to_wide(std::string_view p_text);
// An Int64 against a Number, exactly: below, equal or above as -1, 0 or 1, and not-a-number for one.
double compare_wide(int64_t p_wide, double p_number);

} // namespace vm
} // namespace kas3
