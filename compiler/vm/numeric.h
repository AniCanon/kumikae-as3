// Numbers as AS3 converts and compares them, where C++ alone would not say or would not be safe.
#pragma once

#include "ir.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace kas3 {
namespace vm {

// ECMAScript's ToInteger: NaN is 0, and a fraction goes towards 0.
inline double to_integer(double p_value) {
	return std::isnan(p_value) ? 0 : std::trunc(p_value);
}

// A Number as an Int64, the way a conversion the program wrote does it: not-a-number is 0, and a value
// past either end is held at that end.
int64_t clamp_wide(double p_number);
// Text as an Int64: digits are read exactly, so that every Int64 can be written; anything else is read
// as a Number first, as int("12.7") is.
int64_t string_to_wide(std::string_view p_text);
// The text as an Int64, if it is a whole number that one holds; nothing otherwise.
std::optional<int64_t> string_to_wide_exact(std::string_view p_text);
// An Int64 against a Number, exactly: below, equal or above as -1, 0 or 1, and not-a-number for one.
double compare_wide(int64_t p_wide, double p_number);

// An Int64 as an int, a uint or a Number, and a Number as an Int64, where the value survives the trip;
// nothing where it does not. A Number keeps an Int64 only if it holds the very same value, and gives one
// by dropping its fraction, as for an int.
inline std::optional<int32_t> wide_to_int32(int64_t p_value) {
	return p_value < INT32_MIN || p_value > INT32_MAX ? std::nullopt : std::optional<int32_t>((int32_t)p_value);
}
inline std::optional<uint32_t> wide_to_uint32(int64_t p_value) {
	return p_value < 0 || p_value > (int64_t)UINT32_MAX ? std::nullopt : std::optional<uint32_t>((uint32_t)p_value);
}
inline std::optional<double> wide_to_number(int64_t p_value) {
	const double number = (double)p_value;
	// 2^63 is checked first: converting it back is undefined.
	return number >= 9223372036854775808.0 || (int64_t)number != p_value ? std::nullopt : std::optional<double>(number);
}
inline std::optional<int64_t> number_to_wide(double p_number) {
	return p_number >= -9223372036854775808.0 && p_number < 9223372036854775808.0 ? std::optional<int64_t>((int64_t)p_number) : std::nullopt;
}

// `>>` on an Int64, with the sign carried whatever the compiler does with >> on a negative. The count is
// taken in 0 to 63.
inline int64_t shift_right_wide(int64_t p_value, int64_t p_count) {
	const uint64_t count = (uint64_t)p_count & 63;
	uint64_t bits = (uint64_t)p_value >> count;
	if (p_value < 0 && count > 0) {
		bits |= ~(uint64_t)0 << (64 - count);
	}
	return (int64_t)bits;
}

// Math as AS3 and ECMAScript define it, where C's library says otherwise: round takes a half up
// (round(-2.5) is -2, round(-0.5) is -0); min and max tell -0 from 0 and give not-a-number if either is;
// pow of 1 or -1 to an infinite power, and of anything to not-a-number, is not-a-number.
double math_unary(ir::MathFunction p_function, double p_value);
double math_min(double p_a, double p_b);
double math_max(double p_a, double p_b);
double math_pow(double p_base, double p_power);
// At least 0 and below 1. The interpreter's own generator: the same on every platform, seeded once from
// the clock.
double math_random();

} // namespace vm
} // namespace kas3
