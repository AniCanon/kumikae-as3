#include "text_methods.h"

#include "number_text.h"
#include "numeric.h"
#include "text_units.h"
#include "uri_text.h"

#include <cmath>
#include <string>
#include <string_view>

namespace kas3 {
namespace vm {

using ir::StringFunction;

namespace {

// An index held within 0 and the length.
uint32_t within(double p_index, uint32_t p_length) {
	return p_index <= 0 ? 0 : p_index >= p_length ? p_length : static_cast<uint32_t>(p_index);
}

// An index as slice() takes it: from the end if it is below 0.
uint32_t from_end(double p_index, uint32_t p_length) {
	return within(p_index < 0 ? p_length + p_index : p_index, p_length);
}

// substr(start, count), substring(a, b) and slice(start, end), their arguments made whole.
std::string string_part(StringFunction p_which, std::string_view p_self, uint32_t p_length, double p_first, double p_second) {
	if (p_which == StringFunction::Substr) {
		const uint32_t start = from_end(p_first, p_length);
		return units_between(p_self, start, p_second <= 0 ? start : start + within(p_second, p_length - start));
	}
	if (p_which == StringFunction::Substring) {
		const uint32_t a = within(p_first, p_length), b = within(p_second, p_length);
		return units_between(p_self, a < b ? a : b, a < b ? b : a);
	}
	const uint32_t start = from_end(p_first, p_length), end = from_end(p_second, p_length);
	return units_between(p_self, start, end > start ? end : start);
}

// A String as a method reads it: a missing one reads "null".
std::string_view text_of(const Slot &p_string) {
	const Str *string = static_cast<const Str *>(p_string.p);
	return string ? string->view() : std::string_view("null");
}

bool give_text(std::string_view p_text, Slot *r_out) {
	Str *made = new_string(p_text);
	if (made) {
		r_out->bits = 0;
		r_out->p = made;
	}
	return made != nullptr;
}

bool give_int(int32_t p_value, Slot *r_out) {
	r_out->bits = 0;
	r_out->i = p_value;
	return true;
}

bool give_number(double p_value, Slot *r_out) {
	r_out->d = p_value;
	return true;
}

// charAt: a String of one character, made once for each of the first 128 where they are kept.
bool character(std::string_view p_self, uint32_t p_length, double p_at, Characters *p_characters, Slot *r_out) {
	const std::optional<uint16_t> unit = p_at < 0 || p_at >= p_length ? std::nullopt : unit_at(p_self, static_cast<uint32_t>(p_at));
	if (!unit || *unit >= std::tuple_size<Characters>::value || !p_characters) {
		return give_text(unit ? units_between(p_self, static_cast<uint32_t>(p_at), static_cast<uint32_t>(p_at) + 1) : std::string(), r_out);
	}
	Str *&kept = (*p_characters)[*unit];
	if (!kept) {
		const char one = static_cast<char>(*unit);
		kept = new_string(std::string_view(&one, 1));
	}
	if (!kept) {
		return false;
	}
	retain(kept);
	r_out->bits = 0;
	r_out->p = kept;
	return true;
}

// replace(text, with): AS3 replaces the first match only, with the replacement as it is written. With no
// match, the string itself.
bool replace_first(const Slot &p_string, std::string_view p_pattern, std::string_view p_with, Slot *r_out) {
	const std::string_view self = text_of(p_string);
	const int32_t at = find_units(self, p_pattern, 0);
	if (at < 0) {
		retain(static_cast<Str *>(p_string.p));
		*r_out = p_string;
		return true;
	}
	std::string replaced = units_between(self, 0, static_cast<uint32_t>(at));
	append_text(&replaced, p_with);
	append_text(&replaced, units_between(self, static_cast<uint32_t>(at + utf16_length(p_pattern)), static_cast<uint32_t>(utf16_length(self))));
	return give_text(replaced, r_out);
}

} // namespace

bool reads_text_only(StringFunction p_function) {
	switch (p_function) {
		case StringFunction::Split:
		case StringFunction::EncodeURI:
		case StringFunction::EncodeURIComponent:
		case StringFunction::DecodeURI:
		case StringFunction::DecodeURIComponent:
			return false;
		default:
			return true;
	}
}

bool text_method(StringFunction p_function, const Values &p_values, Characters *p_characters, Slot *r_out) {
	const auto text = [&](size_t p_index) { return text_of(p_values[p_index]); };
	const auto number = [&](size_t p_index) { return p_values[p_index].d; };
	switch (p_function) {
		case StringFunction::FromCharCode: {
			std::string made;
			for (size_t i = 0; i < p_values.count; i++) {
				append_unit(&made, static_cast<uint16_t>(to_uint32(number(i))));
			}
			return give_text(made, r_out);
		}
		case StringFunction::ParseInt:
			return give_number(parse_int(text(0), p_values[1].i), r_out);
		case StringFunction::ParseFloat:
			return give_number(parse_float(text(0)), r_out);
		case StringFunction::Escape:
			return give_text(escape_text(text(0)), r_out);
		case StringFunction::Unescape:
			return give_text(unescape_text(text(0)), r_out);
		default:
			break;
	}
	const std::string_view self = text(0);
	const uint32_t length = static_cast<uint32_t>(utf16_length(self));
	switch (p_function) {
		case StringFunction::CharAt:
			return character(self, length, to_integer(number(1)), p_characters, r_out);
		case StringFunction::CharCodeAt: {
			const double at = to_integer(number(1));
			const std::optional<uint16_t> unit = at < 0 || at >= length ? std::nullopt : unit_at(self, static_cast<uint32_t>(at));
			return give_number(unit ? *unit : std::nan(""), r_out);
		}
		case StringFunction::IndexOf:
			return give_int(find_units(self, text(1), within(to_integer(number(2)), length)), r_out);
		case StringFunction::LastIndexOf: {
			const double from = std::isnan(number(2)) ? INFINITY : to_integer(number(2)); // NaN looks from the end.
			return give_int(find_last_units(self, text(1), within(from, length)), r_out);
		}
		case StringFunction::Substr:
		case StringFunction::Substring:
		case StringFunction::Slice:
			return give_text(string_part(p_function, self, length, to_integer(number(1)), to_integer(number(2))), r_out);
		case StringFunction::Concat: {
			std::string all(self);
			for (size_t i = 1; i < p_values.count; i++) {
				append_text(&all, text(i));
			}
			return give_text(all, r_out);
		}
		case StringFunction::Upper:
			return give_text(upper_case(self), r_out);
		case StringFunction::Lower:
			return give_text(lower_case(self), r_out);
		case StringFunction::Replace:
			return replace_first(p_values[0], text(1), text(2), r_out);
		case StringFunction::LocaleCompare: {
			const int order = compare_utf16(self, text(1));
			return give_int(order < 0 ? -1 : order > 0 ? 1 : 0, r_out);
		}
		default:
			return false; // Not one of these: see reads_text_only().
	}
}

} // namespace vm
} // namespace kas3
