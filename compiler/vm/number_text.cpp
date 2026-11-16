#include "number_text.h"

#include "runtime.h"
#include "text_units.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace kas3 {
namespace vm {

std::string digits_error() {
	return "Error #1002: Number.toPrecision has a range of 1 to 21. Number.toFixed and Number.toExponential have a range of 0 to 20. "
		   "Specified value is not within expected range.";
}

std::string radix_error(double p_radix) {
	return "Error #1003: The radix argument must be between 2 and 36; got " + number_to_string(p_radix) + ".";
}

namespace {

constexpr char DIGITS[] = "0123456789abcdefghijklmnopqrstuvwxyz";

// A whole number of any size, as 32-bit limbs, the lowest first: for every digit of a Number's whole
// part, and for reading digits exactly.
using Limbs = std::vector<uint32_t>;

// p_limbs * p_by + p_add, in place.
void multiply_add(Limbs *p_limbs, uint32_t p_by, uint32_t p_add) {
	uint64_t carry = p_add;
	for (uint32_t &limb : *p_limbs) {
		const uint64_t product = (uint64_t)limb * p_by + carry;
		limb = (uint32_t)product;
		carry = product >> 32;
	}
	if (carry) {
		p_limbs->push_back((uint32_t)carry);
	}
}

// p_limbs / p_by, in place; the remainder.
uint32_t divide(Limbs *p_limbs, uint32_t p_by) {
	uint64_t remainder = 0;
	for (size_t i = p_limbs->size(); i-- > 0;) {
		const uint64_t part = (remainder << 32) | (*p_limbs)[i];
		(*p_limbs)[i] = (uint32_t)(part / p_by);
		remainder = part % p_by;
	}
	while (!p_limbs->empty() && p_limbs->back() == 0) {
		p_limbs->pop_back();
	}
	return (uint32_t)remainder;
}

size_t bit_length(const Limbs &p_limbs) {
	if (p_limbs.empty()) {
		return 0;
	}
	size_t bits = (p_limbs.size() - 1) * 32;
	for (uint32_t top = p_limbs.back(); top; top >>= 1) {
		bits++;
	}
	return bits;
}

// The nearest Number, ties to even, as a conversion from a 64-bit integer rounds.
double limbs_to_number(const Limbs &p_limbs) {
	const size_t bits = bit_length(p_limbs);
	if (bits > 1100) {
		return INFINITY;
	}
	const size_t shift = bits > 64 ? bits - 64 : 0;
	uint64_t top = 0;
	bool below = false; // Any bit set under the 64 kept: it settles a tie, upward.
	for (size_t bit = bits; bit-- > shift;) {
		top = (top << 1) | ((p_limbs[bit / 32] >> (bit % 32)) & 1);
	}
	for (size_t bit = 0; bit < shift && !below; bit++) {
		below = (p_limbs[bit / 32] >> (bit % 32)) & 1;
	}
	return std::ldexp((double)(top | (below ? 1 : 0)), (int)shift);
}

std::string magnitude_text(uint64_t p_value, int p_radix) {
	char buffer[65];
	size_t at = sizeof(buffer);
	do {
		buffer[--at] = DIGITS[p_value % (uint64_t)p_radix];
		p_value /= (uint64_t)p_radix;
	} while (p_value);
	return std::string(buffer + at, sizeof(buffer) - at);
}

// Every digit of a whole Number at least 2^64, which a double holds as 53 bits moved up.
std::string large_whole_text(double p_whole, int p_radix) {
	int exponent = 0;
	const double fraction = std::frexp(p_whole, &exponent);
	Limbs limbs = { 0, 0 };
	const uint64_t bits = (uint64_t)std::ldexp(fraction, 53);
	limbs[0] = (uint32_t)bits;
	limbs[1] = (uint32_t)(bits >> 32);
	for (int i = 53; i < exponent; i++) {
		multiply_add(&limbs, 2, 0);
	}
	std::string reversed;
	while (!limbs.empty()) {
		reversed += DIGITS[divide(&limbs, (uint32_t)p_radix)];
	}
	return std::string(reversed.rbegin(), reversed.rend());
}

// A Number's exact value in decimal: the digits d0 d1 d2 ... (no trailing zeros) of d0.d1d2... x 10^exponent.
// For a Number above zero: printf writes every digit asked for exactly, and none has more than 767.
struct Exact {
	char digits[800];
	int count = 0;
	int exponent = 0;
	char at(int p_index) const { return p_index < count ? digits[p_index] : '0'; }
};

Exact exact_digits(double p_magnitude) {
	char text[820];
	std::snprintf(text, sizeof(text), "%.780e", p_magnitude);
	Exact exact;
	const char *at = text;
	for (; *at && *at != 'e'; at++) {
		if (*at != '.') {
			exact.digits[exact.count++] = *at;
		}
	}
	exact.exponent = std::atoi(at + 1);
	while (exact.count > 1 && exact.digits[exact.count - 1] == '0') {
		exact.count--;
	}
	return exact;
}

// The first p_keep digits, rounded on the next one, a half going up: ECMAScript takes the larger of two
// that are as near. A carry out of the first digit gives one digit more ("999" to "1000").
std::string rounded(const Exact &p_exact, int p_keep) {
	std::string kept;
	for (int i = 0; i < p_keep; i++) {
		kept += p_exact.at(i);
	}
	if (p_keep < 0 || p_exact.at(p_keep) < '5') {
		return kept;
	}
	for (size_t i = kept.size(); i-- > 0;) {
		if (kept[i] != '9') {
			kept[i]++;
			return kept;
		}
		kept[i] = '0';
	}
	return "1" + kept;
}

// d.ddd and an exponent, from n's digits: what toExponential and toPrecision write past their range.
std::string with_exponent(std::string_view p_digits, int p_exponent) {
	std::string out(p_digits.substr(0, 1));
	if (p_digits.size() > 1) {
		out += '.';
		out += p_digits.substr(1);
	}
	return out + (p_exponent < 0 ? "e-" : "e+") + std::to_string(std::abs(p_exponent));
}

// A number as toFixed, toExponential and toPrecision see it: its sign, and its exact digits if it is not 0.
struct Decimal {
	bool negative = false;
	bool zero = true;
	Exact exact;
};

Decimal decimal_of(double p_value) {
	Decimal decimal;
	decimal.negative = p_value < 0;
	decimal.zero = p_value == 0;
	if (!decimal.zero) {
		decimal.exact = exact_digits(std::fabs(p_value));
	}
	return decimal;
}

// An Int64's own digits, every one of them: no Number holds them all past 2^53.
Decimal decimal_of(int64_t p_value) {
	Decimal decimal;
	decimal.negative = p_value < 0;
	decimal.zero = p_value == 0;
	const std::string digits = magnitude_text(p_value < 0 ? 0 - (uint64_t)p_value : (uint64_t)p_value, 10);
	decimal.exact.exponent = (int)digits.size() - 1;
	for (char digit : digits) {
		decimal.exact.digits[decimal.exact.count++] = digit;
	}
	while (decimal.exact.count > 1 && decimal.exact.digits[decimal.exact.count - 1] == '0') {
		decimal.exact.count--;
	}
	return decimal;
}

// The p_count digits of n and the exponent e, for toExponential and toPrecision: n x 10^(e - count + 1)
// is the nearest to the magnitude.
std::string significant(const Decimal &p_decimal, int p_count, int *r_exponent) {
	if (p_decimal.zero) {
		*r_exponent = 0;
		return std::string((size_t)p_count, '0');
	}
	std::string digits = rounded(p_decimal.exact, p_count);
	*r_exponent = p_decimal.exact.exponent;
	if ((int)digits.size() > p_count) {
		digits.pop_back(); // "1000" for three digits is "100", ten times as much.
		(*r_exponent)++;
	}
	return digits;
}

std::string fixed_digits(const Decimal &p_decimal, int p_digits) {
	const std::string sign = p_decimal.negative ? "-" : "";
	// n is the whole number nearest to magnitude x 10^digits: the digits down to the last one wanted.
	std::string n = p_decimal.zero ? "" : rounded(p_decimal.exact, p_decimal.exact.exponent + 1 + p_digits);
	n = n.empty() ? "0" : n;
	if (p_digits == 0) {
		return sign + n;
	}
	if ((int)n.size() <= p_digits) {
		n = std::string((size_t)(p_digits + 1) - n.size(), '0') + n;
	}
	return sign + n.substr(0, n.size() - (size_t)p_digits) + "." + n.substr(n.size() - (size_t)p_digits);
}

std::string exponential_digits(const Decimal &p_decimal, int p_digits) {
	int exponent = 0;
	const std::string digits = significant(p_decimal, p_digits + 1, &exponent);
	return (p_decimal.negative ? "-" : "") + with_exponent(digits, exponent);
}

std::string precision_digits(const Decimal &p_decimal, int p_digits) {
	int e = 0;
	const std::string digits = significant(p_decimal, p_digits, &e);
	const std::string sign = p_decimal.negative ? "-" : "";
	if (e < -6 || e >= p_digits) {
		return sign + with_exponent(digits, e);
	}
	if (e == p_digits - 1) {
		return sign + digits;
	}
	if (e >= 0) {
		return sign + digits.substr(0, (size_t)e + 1) + "." + digits.substr((size_t)e + 1);
	}
	return sign + "0." + std::string((size_t)(-(e + 1)), '0') + digits;
}

} // namespace

std::string radix_text(double p_value, int p_radix) {
	if (p_radix == 10 || !std::isfinite(p_value)) {
		return number_to_string(p_value);
	}
	const double whole = std::trunc(std::fabs(p_value));
	const std::string digits = whole < 18446744073709551616.0 ? magnitude_text((uint64_t)whole, p_radix) : large_whole_text(whole, p_radix);
	return (p_value < 0 && whole != 0 ? "-" : "") + digits;
}

std::string wide_radix_text(int64_t p_value, int p_radix) {
	// The magnitude unsigned: the least Int64 has none of its own.
	const uint64_t magnitude = p_value < 0 ? 0 - (uint64_t)p_value : (uint64_t)p_value;
	return (p_value < 0 ? "-" : "") + magnitude_text(magnitude, p_radix);
}

std::string fixed_text(double p_value, int p_digits) {
	if (std::isnan(p_value) || std::fabs(p_value) >= 1e21) {
		return number_to_string(p_value);
	}
	return fixed_digits(decimal_of(p_value), p_digits);
}

std::string exponential_text(double p_value, int p_digits) {
	return std::isfinite(p_value) ? exponential_digits(decimal_of(p_value), p_digits) : number_to_string(p_value);
}

std::string precision_text(double p_value, int p_digits) {
	return std::isfinite(p_value) ? precision_digits(decimal_of(p_value), p_digits) : number_to_string(p_value);
}

std::string wide_fixed_text(int64_t p_value, int p_digits) {
	return fixed_digits(decimal_of(p_value), p_digits);
}

std::string wide_exponential_text(int64_t p_value, int p_digits) {
	return exponential_digits(decimal_of(p_value), p_digits);
}

std::string wide_precision_text(int64_t p_value, int p_digits) {
	return precision_digits(decimal_of(p_value), p_digits);
}

bool is_text_space(uint32_t p_code) {
	return (p_code >= 9 && p_code <= 13) || p_code == ' ' || p_code == 0xA0 || p_code == 0x1680 || (p_code >= 0x2000 && p_code <= 0x200A) ||
			p_code == 0x2028 || p_code == 0x2029 || p_code == 0x202F || p_code == 0x205F || p_code == 0x3000;
}

std::string_view without_leading_space(std::string_view p_text) {
	size_t at = 0;
	while (at < p_text.size()) {
		size_t size = 0;
		if (!is_text_space(code_point_at(p_text, at, &size))) {
			break;
		}
		at += size;
	}
	return p_text.substr(at);
}

double parse_int(std::string_view p_text, int32_t p_radix) {
	std::string_view text = without_leading_space(p_text);
	const bool negative = !text.empty() && text[0] == '-';
	if (!text.empty() && (text[0] == '-' || text[0] == '+')) {
		text.remove_prefix(1);
	}
	int radix = p_radix == 0 ? 10 : p_radix;
	if (radix < 2 || radix > 36) {
		return std::nan("");
	}
	if ((p_radix == 0 || p_radix == 16) && text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
		text.remove_prefix(2);
		radix = 16;
	}
	Limbs value;
	size_t count = 0;
	for (char c : text) {
		const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 99;
		if (digit >= radix) {
			break;
		}
		count++;
		if (bit_length(value) <= 1100) { // Past that it is Infinity, whatever comes after.
			multiply_add(&value, (uint32_t)radix, (uint32_t)digit);
		}
	}
	if (count == 0) {
		return std::nan("");
	}
	const double magnitude = limbs_to_number(value);
	return negative ? -magnitude : magnitude;
}

double parse_float(std::string_view p_text) {
	const std::string_view text = without_leading_space(p_text);
	size_t at = text.empty() || (text[0] != '+' && text[0] != '-') ? 0 : 1;
	const bool negative = at == 1 && text[0] == '-';
	if (text.substr(at, 8) == "Infinity") {
		return negative ? -INFINITY : INFINITY;
	}
	const auto digits = [&]() {
		const size_t start = at;
		while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
			at++;
		}
		return at - start;
	};
	size_t count = digits();
	if (at < text.size() && text[at] == '.') {
		at++;
		count += digits();
	}
	if (count == 0) {
		return std::nan("");
	}
	size_t end = at;
	if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
		at++;
		at += at < text.size() && (text[at] == '+' || text[at] == '-') ? 1 : 0;
		end = digits() ? at : end; // "1e" and "1e+" are 1.
	}
	return std::strtod(std::string(text.substr(0, end)).c_str(), nullptr);
}

} // namespace vm
} // namespace kas3
