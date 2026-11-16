#include "numeric.h"

#include "runtime.h"

namespace kas3 {
namespace vm {

int64_t clamp_wide(double p_number) {
	if (p_number != p_number) {
		return 0;
	}
	if (p_number >= 9223372036854775808.0) {
		return INT64_MAX;
	}
	if (p_number < -9223372036854775808.0) {
		return INT64_MIN;
	}
	return (int64_t)p_number;
}

// Whole digits, read exactly: the value if they fit an Int64, nothing for text that is not only digits or
// for digits past the largest (r_past then says so).
static std::optional<int64_t> wide_digits(std::string_view p_text, bool *r_past) {
	size_t start = 0, end = p_text.size();
	while (start < end && (p_text[start] == ' ' || p_text[start] == '\t' || p_text[start] == '\n' || p_text[start] == '\r')) {
		start++;
	}
	while (end > start && (p_text[end - 1] == ' ' || p_text[end - 1] == '\t' || p_text[end - 1] == '\n' || p_text[end - 1] == '\r')) {
		end--;
	}
	size_t at = start;
	bool negative = at < end && p_text[at] == '-';
	if (at < end && (p_text[at] == '-' || p_text[at] == '+')) {
		at++;
	}
	// Hex, as AS3 reads "0x10".
	const bool hex = at + 1 < end && p_text[at] == '0' && (p_text[at + 1] == 'x' || p_text[at + 1] == 'X');
	const uint64_t base = hex ? 16 : 10;
	at += hex ? 2 : 0;
	bool digits = at < end;
	uint64_t value = 0;
	const uint64_t limit = negative ? (uint64_t)1 << 63 : (uint64_t)INT64_MAX;
	bool past = false;
	for (size_t i = at; i < end && digits; i++) {
		const char c = p_text[i];
		const uint64_t digit = c >= '0' && c <= '9' ? (uint64_t)(c - '0')
				: hex && c >= 'a' && c <= 'f' ? (uint64_t)(c - 'a' + 10)
				: hex && c >= 'A' && c <= 'F' ? (uint64_t)(c - 'A' + 10)
											  : base;
		if (digit >= base) {
			digits = false;
		} else if (!past) {
			past = value > (limit - digit) / base;
			value = past ? limit : value * base + digit;
		}
	}
	*r_past = digits && past;
	if (!digits || past) {
		return std::nullopt;
	}
	return negative ? (int64_t)(0 - value) : (int64_t)value;
}

int64_t string_to_wide(std::string_view p_text) {
	bool past = false;
	if (const std::optional<int64_t> exact = wide_digits(p_text, &past)) {
		return *exact;
	}
	return clamp_wide(string_to_number(p_text)); // Past the largest, the number is too: it is held there.
}

std::optional<int64_t> string_to_wide_exact(std::string_view p_text) {
	// Whole digits that fit read exactly; anything else is read as a Number, which must be whole and fit.
	bool past = false;
	if (const std::optional<int64_t> exact = wide_digits(p_text, &past)) {
		return exact;
	}
	return past ? std::nullopt : number_to_wide(string_to_number(p_text));
}

double compare_wide(int64_t p_wide, double p_number) {
	if (p_number != p_number) {
		return p_number;
	}
	if (p_number >= 9223372036854775808.0) {
		return -1;
	}
	if (p_number < -9223372036854775808.0) {
		return 1;
	}
	int64_t whole = (int64_t)p_number; // Towards zero.
	if (p_wide != whole) {
		return p_wide < whole ? -1 : 1;
	}
	double fraction = p_number - (double)whole;
	return fraction > 0 ? -1 : (fraction < 0 ? 1 : 0);
}

} // namespace vm
} // namespace kas3
