#include "interpreter_internal.h"

#include "number_text.h"
#include "text_units.h"

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
	if (which == StringFunction::ParseInt || which == StringFunction::ParseFloat) {
		out.d = which == StringFunction::ParseInt ? parse_int(text(0), p_registers[list[1]].i) : parse_float(text(0));
		return;
	}
	const std::string_view self = text(0);
	const uint32_t length = static_cast<uint32_t>(utf16_length(self));
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

void Interpreter::number_text(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const Slot value = p_registers[list[0]];
	const double digits = whole(p_registers[list[1]].d);
	const NumberFormat format = static_cast<NumberFormat>(p_in.b);
	std::string made;
	if (format == NumberFormat::Radix || format == NumberFormat::WideRadix) {
		if (digits < 2 || digits > 36) {
			raise(module_.range_error_class, radix_error(digits));
			return;
		}
		made = format == NumberFormat::Radix ? radix_text(value.d, static_cast<int>(digits)) : wide_radix_text(value.l, static_cast<int>(digits));
	} else if (format != NumberFormat::Fixed && !std::isfinite(value.d)) {
		made = number_to_string(value.d); // toExponential and toPrecision look at NaN and Infinity before the digits.
	} else {
		const double least = format == NumberFormat::Precision ? 1 : 0;
		const double most = format == NumberFormat::Precision ? MAX_PRECISION : MAX_FIXED_DIGITS;
		if (digits < least || digits > most) {
			raise(module_.range_error_class, digits_error());
			return;
		}
		const int count = static_cast<int>(digits);
		made = format == NumberFormat::Fixed ? fixed_text(value.d, count)
				: format == NumberFormat::Exponential ? exponential_text(value.d, count)
													   : precision_text(value.d, count);
	}
	assign(Kind::Str, p_registers[p_in.a], text(made));
}

void Interpreter::any_null_check(Slot p_value) {
	const AnyTag tag = any::tag(p_value);
	if (tag == AnyTag::Undefined) {
		raise(module_.type_error_class, "Error #1010: A term is undefined and has no properties.");
	} else if (tag == AnyTag::Null) {
		raise_null();
	}
}

} // namespace vm
} // namespace kas3
