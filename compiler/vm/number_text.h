// Numbers as text and text as numbers, as the methods of Number, int, uint and Int64 and the top-level
// functions of AS3 give them. ECMAScript has the same rules, with the differences said below.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kas3 {
namespace vm {

// The digits each method takes, and what AS3 says when they are outside: RangeError #1002 and #1003.
constexpr int MAX_FIXED_DIGITS = 20; // toFixed and toExponential: 0 to 20.
constexpr int MAX_PRECISION = 21; // toPrecision: 1 to 21.
std::string digits_error();
std::string radix_error(double p_radix);

// toString(radix), radix 2 to 36. In a radix other than 10, AS3 writes only the whole part of the number
// (ECMAScript today writes the fraction too): every digit of it, exactly.
std::string radix_text(double p_value, int p_radix);
// An Int64's own 64 bits, exactly.
std::string wide_radix_text(int64_t p_value, int p_radix);
// toFixed, toExponential and toPrecision, digits in range. A tie rounds away from zero, as ECMAScript
// says, from the number's exact value: (2.5).toFixed(0) is "3", and (1.005).toFixed(2) is "1.00".
std::string fixed_text(double p_value, int p_digits);
std::string exponential_text(double p_value, int p_digits);
std::string precision_text(double p_value, int p_digits);
// The same for an Int64, from its own digits: exact past 2^53, where a Number is not.
std::string wide_fixed_text(int64_t p_value, int p_digits);
std::string wide_exponential_text(int64_t p_value, int p_digits);
std::string wide_precision_text(int64_t p_value, int p_digits);

// Whether ECMAScript's String-to-Number rules take the character as white space: ASCII's, the no-break
// space, Unicode's other spaces and the line and paragraph separators.
bool is_text_space(uint32_t p_code_point);
// The text after the white space it starts with.
std::string_view without_leading_space(std::string_view p_text);
// parseInt: radix 0 for 10 (or 16 after "0x"), else 2 to 36. The digits are read exactly: the result is
// the nearest Number to the whole number they write. A leading 0 is not octal, as AS3's reference says.
double parse_int(std::string_view p_text, int32_t p_radix);
// parseFloat: the longest start of the text, after white space, that is a decimal number or Infinity.
double parse_float(std::string_view p_text);

} // namespace vm
} // namespace kas3
