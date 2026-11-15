#include "text_units.h"

#include "runtime.h"

#include <cstring>

namespace kas3 {
namespace vm {

namespace {

bool is_high(uint32_t p_unit) {
	return p_unit >= 0xD800 && p_unit < 0xDC00;
}

bool is_low(uint32_t p_unit) {
	return p_unit >= 0xDC00 && p_unit < 0xE000;
}

uint16_t high_half(uint32_t p_code_point) {
	return static_cast<uint16_t>(0xD800 + ((p_code_point - 0x10000) >> 10));
}

uint16_t low_half(uint32_t p_code_point) {
	return static_cast<uint16_t>(0xDC00 + ((p_code_point - 0x10000) & 0x3FF));
}

// Where unit p_unit starts: its byte, and whether it is the low half of the four-byte character there.
struct UnitPlace {
	size_t byte = 0;
	bool low_of_pair = false;
};

UnitPlace place_of_unit(std::string_view p_text, uint32_t p_unit) {
	uint32_t units = 0;
	size_t at = 0;
	while (at < p_text.size() && units < p_unit) {
		size_t size = 0;
		const uint32_t code = code_point_at(p_text, at, &size);
		if (code >= 0x10000 && units + 1 == p_unit) {
			return { at, true };
		}
		units += code >= 0x10000 ? 2 : 1;
		at += size;
	}
	return { at, false };
}

// The text as UTF-16 units: for the searches whose needle begins or ends with a half held alone, which
// may be one half of a character in the text.
std::u16string units_of(std::string_view p_text) {
	std::u16string units;
	units.reserve(p_text.size());
	for (size_t at = 0; at < p_text.size();) {
		size_t size = 0;
		const uint32_t code = code_point_at(p_text, at, &size);
		if (code >= 0x10000) {
			units += static_cast<char16_t>(high_half(code));
			units += static_cast<char16_t>(low_half(code));
		} else {
			units += static_cast<char16_t>(code);
		}
		at += size;
	}
	return units;
}

// Whether a byte search finds the needle exactly where a search by units would: it does unless the
// needle starts with a low half or ends with a high half, which in the text may belong to a character.
bool bytes_will_do(std::string_view p_needle) {
	size_t size = 0;
	if (is_low(code_point_at(p_needle, 0, &size))) {
		return false;
	}
	size_t last = p_needle.size() - 1;
	while (last > 0 && (static_cast<unsigned char>(p_needle[last]) & 0xC0) == 0x80) {
		last--;
	}
	return !is_high(code_point_at(p_needle, last, &size));
}

template <class Map>
std::string changed_case(std::string_view p_text, Map p_map) {
	std::string out;
	out.reserve(p_text.size());
	for (size_t at = 0; at < p_text.size();) {
		size_t size = 0;
		const uint32_t code = code_point_at(p_text, at, &size);
		const uint32_t mapped = code < 0x10000 ? p_map(code) : code;
		if (mapped == code) {
			out.append(p_text.substr(at, size));
		} else {
			encode(&out, mapped);
		}
		at += size;
	}
	return out;
}

} // namespace

uint32_t code_point_at(std::string_view p_text, size_t p_at, size_t *r_size) {
	const unsigned char lead = static_cast<unsigned char>(p_text[p_at]);
	const size_t size = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
	if (p_at + size > p_text.size()) {
		*r_size = 1;
		return lead;
	}
	uint32_t value = size == 1 ? lead : lead & (0xFF >> (size + 1));
	for (size_t i = 1; i < size; i++) {
		value = (value << 6) | (static_cast<unsigned char>(p_text[p_at + i]) & 0x3F);
	}
	*r_size = size;
	return value;
}

void encode(std::string *r_text, uint32_t p_code) {
	if (p_code < 0x80) {
		*r_text += static_cast<char>(p_code);
	} else if (p_code < 0x800) {
		*r_text += static_cast<char>(0xC0 | (p_code >> 6));
		*r_text += static_cast<char>(0x80 | (p_code & 0x3F));
	} else if (p_code < 0x10000) {
		*r_text += static_cast<char>(0xE0 | (p_code >> 12));
		*r_text += static_cast<char>(0x80 | ((p_code >> 6) & 0x3F));
		*r_text += static_cast<char>(0x80 | (p_code & 0x3F));
	} else {
		*r_text += static_cast<char>(0xF0 | (p_code >> 18));
		*r_text += static_cast<char>(0x80 | ((p_code >> 12) & 0x3F));
		*r_text += static_cast<char>(0x80 | ((p_code >> 6) & 0x3F));
		*r_text += static_cast<char>(0x80 | (p_code & 0x3F));
	}
}

std::optional<uint16_t> unit_at(std::string_view p_text, uint32_t p_index) {
	const UnitPlace place = place_of_unit(p_text, p_index);
	if (place.byte >= p_text.size()) {
		return std::nullopt;
	}
	size_t size = 0;
	const uint32_t code = code_point_at(p_text, place.byte, &size);
	if (code < 0x10000) {
		return static_cast<uint16_t>(code);
	}
	return place.low_of_pair ? low_half(code) : high_half(code);
}

std::string units_between(std::string_view p_text, uint32_t p_start, uint32_t p_end) {
	std::string out;
	if (p_start >= p_end) {
		return out;
	}
	const UnitPlace first = place_of_unit(p_text, p_start);
	uint32_t units = p_start;
	size_t at = first.byte;
	if (first.low_of_pair) {
		size_t size = 0;
		encode(&out, low_half(code_point_at(p_text, at, &size)));
		at += size;
		units++;
	}
	const size_t from = at;
	while (at < p_text.size() && units < p_end) {
		size_t size = 0;
		const uint32_t code = code_point_at(p_text, at, &size);
		if (code >= 0x10000 && units + 1 == p_end) {
			out.append(p_text.substr(from, at - from));
			encode(&out, high_half(code)); // Cut in the middle: the high half alone.
			return out;
		}
		units += code >= 0x10000 ? 2 : 1;
		at += size;
	}
	out.append(p_text.substr(from, at - from));
	return out;
}

size_t joined_halves(std::string_view p_first, std::string_view p_second) {
	// A lone half is 0xED and then 0xA0 to 0xAF for a high one, 0xB0 to 0xBF for a low one.
	if (p_first.size() < 3 || p_second.size() < 3) {
		return 0;
	}
	const unsigned char *end = reinterpret_cast<const unsigned char *>(p_first.data() + p_first.size() - 3);
	const unsigned char *start = reinterpret_cast<const unsigned char *>(p_second.data());
	return end[0] == 0xED && (end[1] & 0xF0) == 0xA0 && start[0] == 0xED && (start[1] & 0xF0) == 0xB0 ? 2 : 0;
}

void write_joined(std::string_view p_first, std::string_view p_second, char *r_four) {
	size_t size = 0;
	const uint32_t high = code_point_at(p_first, p_first.size() - 3, &size);
	const uint32_t low = code_point_at(p_second, 0, &size);
	std::string four;
	encode(&four, 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00));
	std::memcpy(r_four, four.data(), 4);
}

void append_text(std::string *r_text, std::string_view p_text) {
	if (joined_halves(*r_text, p_text)) {
		char four[4];
		write_joined(*r_text, p_text, four);
		r_text->resize(r_text->size() - 3);
		r_text->append(four, 4);
		p_text.remove_prefix(3);
	}
	r_text->append(p_text);
}

void append_unit(std::string *r_text, uint16_t p_unit) {
	std::string unit;
	encode(&unit, p_unit);
	append_text(r_text, unit);
}

int32_t find_units(std::string_view p_text, std::string_view p_needle, uint32_t p_from) {
	const uint32_t length = static_cast<uint32_t>(utf16_length(p_text));
	const uint32_t from = p_from < length ? p_from : length;
	if (p_needle.empty()) {
		return static_cast<int32_t>(from);
	}
	if (bytes_will_do(p_needle)) {
		// A needle that starts with no low half can't start inside a character: past it, if unit p_from is
		// the low half of one.
		const UnitPlace place = place_of_unit(p_text, from);
		size_t start = place.byte;
		if (place.low_of_pair) {
			size_t size = 0;
			code_point_at(p_text, start, &size);
			start += size;
		}
		const size_t found = p_text.find(p_needle, start);
		return found == std::string_view::npos ? -1 : utf16_length(p_text.substr(0, found));
	}
	const std::u16string units = units_of(p_text);
	const size_t found = units.find(units_of(p_needle), from);
	return found == std::u16string::npos ? -1 : static_cast<int32_t>(found);
}

int32_t find_last_units(std::string_view p_text, std::string_view p_needle, uint32_t p_from) {
	const uint32_t length = static_cast<uint32_t>(utf16_length(p_text));
	const uint32_t from = p_from < length ? p_from : length;
	if (p_needle.empty()) {
		return static_cast<int32_t>(from);
	}
	if (bytes_will_do(p_needle)) {
		// A match may start at the character whose low half unit p_from is: its high half is before it.
		const size_t found = p_text.rfind(p_needle, place_of_unit(p_text, from).byte);
		return found == std::string_view::npos ? -1 : utf16_length(p_text.substr(0, found));
	}
	const std::u16string units = units_of(p_text);
	const size_t found = units.rfind(units_of(p_needle), from);
	return found == std::u16string::npos ? -1 : static_cast<int32_t>(found);
}

std::string upper_case(std::string_view p_text) {
	return changed_case(p_text, to_upper);
}

std::string lower_case(std::string_view p_text) {
	return changed_case(p_text, to_lower);
}

} // namespace vm
} // namespace kas3
