#include "uri_text.h"

#include "text_units.h"

namespace kas3 {
namespace vm {

namespace {

constexpr std::string_view HEX = "0123456789ABCDEF";
constexpr std::string_view ESCAPE_KEEPS = "@*_+-./";
constexpr std::string_view UNRESERVED_MARKS = "-_.!~*'()";
constexpr std::string_view RESERVED = ";/?:@&=+$,#"; // With #, which a whole URI keeps too.

bool is_alphanumeric(uint32_t p_unit) {
	return (p_unit >= '0' && p_unit <= '9') || (p_unit >= 'A' && p_unit <= 'Z') || (p_unit >= 'a' && p_unit <= 'z');
}

bool one_of(std::string_view p_set, uint32_t p_unit) {
	return p_unit < 0x80 && p_set.find(static_cast<char>(p_unit)) != std::string_view::npos;
}

std::optional<uint32_t> hex_digit(char16_t p_unit) {
	if (p_unit >= u'0' && p_unit <= u'9') {
		return p_unit - u'0';
	}
	if ((p_unit >= u'a' && p_unit <= u'f') || (p_unit >= u'A' && p_unit <= u'F')) {
		return (p_unit | 0x20) - u'a' + 10;
	}
	return std::nullopt;
}

// The number the p_digits hex digits from p_at write, if they are all there.
std::optional<uint32_t> hex_at(std::u16string_view p_units, size_t p_at, size_t p_digits) {
	if (p_at + p_digits > p_units.size()) {
		return std::nullopt;
	}
	uint32_t value = 0;
	for (size_t i = 0; i < p_digits; i++) {
		const std::optional<uint32_t> digit = hex_digit(p_units[p_at + i]);
		if (!digit) {
			return std::nullopt;
		}
		value = value * 16 + *digit;
	}
	return value;
}

void add_hex(std::string *r_text, uint32_t p_value, size_t p_digits) {
	for (size_t i = p_digits; i-- > 0;) {
		*r_text += HEX[(p_value >> (4 * i)) & 0xF];
	}
}

// One code point's UTF-8 bytes, each as %XX.
void add_encoded(std::string *r_text, uint32_t p_code_point) {
	std::string bytes;
	encode(&bytes, p_code_point);
	for (const char byte : bytes) {
		*r_text += '%';
		add_hex(r_text, static_cast<unsigned char>(byte), 2);
	}
}

// The code point of UTF-8 bytes written as %XX from p_at, where the first byte, already read, is p_lead:
// nothing unless they are UTF-8 (no byte too many, no surrogate, nothing past U+10FFFF). *r_end is where
// the sequence ends.
std::optional<uint32_t> decoded_bytes(std::u16string_view p_units, size_t p_at, uint32_t p_lead, size_t *r_end) {
	const int count = p_lead >= 0xF0 && p_lead < 0xF8 ? 4 : p_lead >= 0xE0 ? 3 : p_lead >= 0xC0 ? 2 : 0;
	if (count == 0 || p_lead >= 0xF8) {
		return std::nullopt;
	}
	uint32_t value = p_lead & (0xFF >> (count + 1));
	size_t at = p_at;
	for (int i = 1; i < count; i++, at += 3) {
		const std::optional<uint32_t> byte = at < p_units.size() && p_units[at] == u'%' ? hex_at(p_units, at + 1, 2) : std::nullopt;
		if (!byte || (*byte & 0xC0) != 0x80) {
			return std::nullopt;
		}
		value = (value << 6) | (*byte & 0x3F);
	}
	constexpr uint32_t least[] = { 0, 0, 0x80, 0x800, 0x10000 };
	if (value < least[count] || value > 0x10FFFF || (value >= 0xD800 && value < 0xE000)) {
		return std::nullopt;
	}
	*r_end = at;
	return value;
}

} // namespace

std::string escape_text(std::string_view p_text) {
	std::string out;
	for (const char16_t unit : units_of(p_text)) {
		if (is_alphanumeric(unit) || one_of(ESCAPE_KEEPS, unit)) {
			out += static_cast<char>(unit);
		} else if (unit < 0x100) {
			out += '%';
			add_hex(&out, unit, 2);
		} else {
			out += "%u";
			add_hex(&out, unit, 4);
		}
	}
	return out;
}

std::string unescape_text(std::string_view p_text) {
	const std::u16string units = units_of(p_text);
	std::string out;
	for (size_t at = 0; at < units.size(); at++) {
		std::optional<uint32_t> unit;
		if (units[at] == u'%' && at + 1 < units.size() && units[at + 1] == u'u' && (unit = hex_at(units, at + 2, 4))) {
			at += 5;
		} else if (units[at] == u'%' && (unit = hex_at(units, at + 1, 2))) {
			at += 2;
		}
		append_unit(&out, static_cast<uint16_t>(unit.value_or(units[at])));
	}
	return out;
}

std::optional<std::string> encode_uri(std::string_view p_text, bool p_component) {
	const std::u16string units = units_of(p_text);
	std::string out;
	for (size_t at = 0; at < units.size(); at++) {
		const char16_t unit = units[at];
		if (is_alphanumeric(unit) || one_of(UNRESERVED_MARKS, unit) || (!p_component && one_of(RESERVED, unit))) {
			out += static_cast<char>(unit);
			continue;
		}
		if (unit >= 0xDC00 && unit < 0xE000) {
			return std::nullopt;
		}
		uint32_t code_point = unit;
		if (unit >= 0xD800 && unit < 0xDC00) {
			if (at + 1 >= units.size() || units[at + 1] < 0xDC00 || units[at + 1] >= 0xE000) {
				return std::nullopt;
			}
			code_point = 0x10000 + ((unit - 0xD800) << 10) + (units[++at] - 0xDC00);
		}
		add_encoded(&out, code_point);
	}
	return out;
}

std::optional<std::string> decode_uri(std::string_view p_text, bool p_component) {
	const std::u16string units = units_of(p_text);
	std::string out;
	for (size_t at = 0; at < units.size();) {
		if (units[at] != u'%') {
			append_unit(&out, units[at++]);
			continue;
		}
		const std::optional<uint32_t> lead = hex_at(units, at + 1, 2);
		if (!lead) {
			return std::nullopt;
		}
		if (*lead < 0x80) {
			// A reserved character written with % stays written so in a whole URI: it may mean something there.
			if (!p_component && one_of(RESERVED, *lead)) {
				out += '%';
				out += static_cast<char>(units[at + 1]);
				out += static_cast<char>(units[at + 2]);
			} else {
				out += static_cast<char>(*lead);
			}
			at += 3;
			continue;
		}
		size_t end = 0;
		const std::optional<uint32_t> code_point = decoded_bytes(units, at + 3, *lead, &end);
		if (!code_point) {
			return std::nullopt;
		}
		encode(&out, *code_point);
		at = end;
	}
	return out;
}

std::string uri_error(std::string_view p_function) {
	return "Error #1052: Invalid URI passed to " + std::string(p_function) + " function.";
}

} // namespace vm
} // namespace kas3
