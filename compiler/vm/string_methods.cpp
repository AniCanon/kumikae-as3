#include "interpreter_internal.h"

#include "number_text.h"
#include "text_units.h"
#include "uri_text.h"

#include <cmath>

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NumberFormat;
using ir::StringFunction;

namespace {

// ECMAScript's ToInteger: NaN is 0, and a fraction goes towards 0.
double whole(double p_value) {
	return std::isnan(p_value) ? 0 : std::trunc(p_value);
}

// An index held within 0 and the length.
uint32_t within(double p_index, uint32_t p_length) {
	return p_index <= 0 ? 0 : p_index >= p_length ? p_length : static_cast<uint32_t>(p_index);
}

// An index as slice() takes it: from the end if it is below 0.
uint32_t from_end(double p_index, uint32_t p_length) {
	return within(p_index < 0 ? p_length + p_index : p_index, p_length);
}

std::string_view text_in(Slot p_string) {
	const Str *string = static_cast<const Str *>(p_string.p);
	return string ? string->view() : std::string_view();
}

Slot of_pointer(void *p_value) {
	Slot slot;
	slot.bits = 0;
	slot.p = p_value;
	return slot;
}

constexpr std::string_view URI_FUNCTIONS[] = { "encodeURI", "encodeURIComponent", "decodeURI", "decodeURIComponent" };

} // namespace

void Interpreter::string_call(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const auto text = [&](size_t p_index) {
		const Str *string = static_cast<const Str *>(p_registers[list[p_index]].p);
		return string ? string->view() : std::string_view("null");
	};
	const auto number = [&](size_t p_index) { return p_registers[list[p_index]].d; };
	Slot &out = p_registers[p_in.a];
	const auto give_text = [&](const std::string &p_text) { assign(Kind::Str, out, this->text(p_text)); };
	const auto give_int = [&](int32_t p_value) { set_int32(out, p_value); };
	const StringFunction which = static_cast<StringFunction>(p_in.b);
	if (which == StringFunction::FromCharCode) {
		std::string made;
		for (size_t i = 0; i < list.size(); i++) {
			append_unit(&made, static_cast<uint16_t>(to_uint32(number(i))));
		}
		give_text(made);
		return;
	}
	if (which == StringFunction::Split) {
		string_split(p_function, p_in);
		return;
	}
	if (which == StringFunction::ParseInt || which == StringFunction::ParseFloat) {
		out.d = which == StringFunction::ParseInt ? parse_int(text(0), p_registers[list[1]].i) : parse_float(text(0));
		return;
	}
	const std::string_view self = text(0);
	const uint32_t length = static_cast<uint32_t>(utf16_length(self));
	if (which >= StringFunction::Escape) {
		uri_function(which, self, out);
		return;
	}
	switch (which) {
		case StringFunction::CharAt:
		case StringFunction::CharCodeAt: {
			const double at = whole(number(1));
			const std::optional<uint16_t> unit = at < 0 || at >= length ? std::nullopt : unit_at(self, static_cast<uint32_t>(at));
			if (which == StringFunction::CharCodeAt) {
				out.d = unit ? *unit : std::nan("");
			} else if (unit && *unit < characters_.size()) {
				Str *&character = characters_[*unit];
				character = character ? character : this->text(std::string(1, static_cast<char>(*unit)));
				retain(character);
				assign(Kind::Str, out, character);
			} else {
				give_text(unit ? units_between(self, static_cast<uint32_t>(at), static_cast<uint32_t>(at) + 1) : std::string());
			}
			break;
		}
		case StringFunction::IndexOf:
			give_int(find_units(self, text(1), within(whole(number(2)), length)));
			break;
		case StringFunction::LastIndexOf: {
			const double from = std::isnan(number(2)) ? INFINITY : whole(number(2)); // NaN looks from the end.
			give_int(find_last_units(self, text(1), within(from, length)));
			break;
		}
		case StringFunction::Substr: {
			const uint32_t start = from_end(whole(number(1)), length);
			const double count = whole(number(2));
			give_text(units_between(self, start, count <= 0 ? start : start + within(count, length - start)));
			break;
		}
		case StringFunction::Substring: {
			const uint32_t a = within(whole(number(1)), length), b = within(whole(number(2)), length);
			give_text(units_between(self, a < b ? a : b, a < b ? b : a));
			break;
		}
		case StringFunction::Slice: {
			const uint32_t start = from_end(whole(number(1)), length), end = from_end(whole(number(2)), length);
			give_text(units_between(self, start, end > start ? end : start));
			break;
		}
		case StringFunction::Concat: {
			std::string all(self);
			for (size_t i = 1; i < list.size(); i++) {
				append_text(&all, text(i));
			}
			give_text(all);
			break;
		}
		case StringFunction::Upper:
		case StringFunction::Lower:
			give_text(which == StringFunction::Upper ? upper_case(self) : lower_case(self));
			break;
		case StringFunction::Replace: {
			// AS3 replaces the first match only, with the replacement as it is written.
			const int32_t at = find_units(self, text(1), 0);
			if (at < 0) {
				assign(Kind::Str, out, share(Kind::Str, p_registers[list[0]]).p);
				break;
			}
			std::string replaced = units_between(self, 0, static_cast<uint32_t>(at));
			append_text(&replaced, text(2));
			append_text(&replaced, units_between(self, static_cast<uint32_t>(at + utf16_length(text(1))), length));
			give_text(replaced);
			break;
		}
		case StringFunction::LocaleCompare: {
			const int order = compare_utf16(self, text(1));
			give_int(order < 0 ? -1 : order > 0 ? 1 : 0);
			break;
		}
		default:
			break;
	}
}

// escape, unescape and the URI functions. One that can't read or write its text is AS3's URIError #1052.
void Interpreter::uri_function(StringFunction p_which, std::string_view p_text, Slot &r_out) {
	if (p_which == StringFunction::Escape || p_which == StringFunction::Unescape) {
		assign(Kind::Str, r_out, text(p_which == StringFunction::Escape ? escape_text(p_text) : unescape_text(p_text)));
		return;
	}
	const size_t uri = static_cast<size_t>(p_which) - static_cast<size_t>(StringFunction::EncodeURI);
	const bool component = p_which == StringFunction::EncodeURIComponent || p_which == StringFunction::DecodeURIComponent;
	const std::optional<std::string> made = uri < 2 ? encode_uri(p_text, component) : decode_uri(p_text, component);
	if (made) {
		assign(Kind::Str, r_out, text(*made));
	} else {
		raise(module_.uri_error_class != ir::NONE ? module_.uri_error_class : module_.error_class, uri_error(URI_FUNCTIONS[uri]));
	}
}

namespace {

// A number in the digits asked for, which are in range: from a Number, or from an Int64's own digits.
std::string formatted(NumberFormat p_format, Slot p_value, bool p_wide, int p_digits) {
	switch (p_format) {
		case NumberFormat::Radix:
			return p_wide ? wide_radix_text(p_value.l, p_digits) : radix_text(p_value.d, p_digits);
		case NumberFormat::Fixed:
			return p_wide ? wide_fixed_text(p_value.l, p_digits) : fixed_text(p_value.d, p_digits);
		case NumberFormat::Exponential:
			return p_wide ? wide_exponential_text(p_value.l, p_digits) : exponential_text(p_value.d, p_digits);
		case NumberFormat::Precision:
			return p_wide ? wide_precision_text(p_value.l, p_digits) : precision_text(p_value.d, p_digits);
	}
	return std::string();
}

} // namespace

void Interpreter::number_text(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const Slot value = p_registers[list[0]];
	const bool wide = p_function.registers[list[0]] == Kind::I64;
	const double digits = whole(p_registers[list[1]].d);
	const NumberFormat format = static_cast<NumberFormat>(p_in.b);
	double least = 0, most = MAX_FIXED_DIGITS;
	if (format == NumberFormat::Radix) {
		least = 2;
		most = 36;
	} else if (format == NumberFormat::Precision) {
		least = 1;
		most = MAX_PRECISION;
	}
	if (!wide && (format == NumberFormat::Exponential || format == NumberFormat::Precision) && !std::isfinite(value.d)) {
		// toExponential and toPrecision look at NaN and Infinity before the digits.
		assign(Kind::Str, p_registers[p_in.a], text(number_to_string(value.d)));
		return;
	}
	if (digits < least || digits > most) {
		raise(module_.range_error_class, format == NumberFormat::Radix ? radix_error(digits) : digits_error());
		return;
	}
	assign(Kind::Str, p_registers[p_in.a], text(formatted(format, value, wide, static_cast<int>(digits))));
}

void Interpreter::any_null_check(Slot p_value) {
	const AnyTag tag = any::tag(p_value);
	if (tag == AnyTag::Undefined) {
		raise(module_.type_error_class, "Error #1010: A term is undefined and has no properties.");
	} else if (tag == AnyTag::Null) {
		raise_null();
	}
}

// split(delimiter, limit), as ECMAScript 3 has it: a RegExp splits as split_by_pattern() says; none
// (null or undefined) is the whole string; an empty one gives each UTF-16 unit; at most `limit` pieces. A
// delimiter that is neither a String nor null becomes its text, which may run the program: the registers
// are read again after.
void Interpreter::string_split(const Function &p_function, const Instruction &p_in) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const size_t base = frames_.back().base;
	const auto at = [&](size_t p_index) -> Slot & { return stack_[base + list[p_index]]; };
	if (is_regexp(at(1))) {
		Vector *made = split_by_pattern(static_cast<Object *>(any::pointer(at(1))), text_in(at(0)), to_uint32(at(2).d));
		if (made) {
			put(Kind::Obj, stack_[base + p_in.a], of_pointer(made));
		}
		return;
	}
	const AnyTag tag = any::tag(at(1));
	const bool whole = tag == AnyTag::Null || tag == AnyTag::Undefined;
	std::string by;
	if (!whole) {
		const Slot held = share(Kind::Any, at(1));
		const bool said = any_text(held, &by);
		drop_any(held);
		if (!said) {
			return;
		}
	}
	const Str *string = static_cast<const Str *>(at(0).p);
	const std::string_view self = string ? string->view() : std::string_view();
	const uint32_t most = to_uint32(at(2).d);
	const uint32_t length = static_cast<uint32_t>(utf16_length(self));
	std::vector<std::string> pieces;
	if (whole) {
		pieces.emplace_back(self);
	} else if (by.empty()) {
		for (uint32_t i = 0; i < length && pieces.size() < most; i++) {
			pieces.push_back(units_between(self, i, i + 1));
		}
	} else if (length > 0) {
		const uint32_t step = static_cast<uint32_t>(utf16_length(by));
		uint32_t from = 0;
		while (pieces.size() < most) {
			const int32_t found = find_units(self, by, from);
			const uint32_t end = found < 0 ? length : static_cast<uint32_t>(found);
			pieces.push_back(units_between(self, from, end));
			if (found < 0) {
				break;
			}
			from = end + step;
		}
	} else {
		pieces.emplace_back(); // An empty string with a delimiter: one empty piece.
	}
	if (pieces.size() > most) {
		pieces.resize(most);
	}
	Vector *made = fresh_vector(module_.array_class, pieces.size());
	for (size_t i = 0; made && i < pieces.size(); i++) {
		Str *piece = text(pieces[i]);
		if (!piece) {
			release_object(made);
			out_of_memory();
			return;
		}
		made->data[i] = any::string(piece);
	}
	if (made) {
		put(Kind::Obj, stack_[base + p_in.a], of_pointer(made));
	}
}

} // namespace vm
} // namespace kas3
