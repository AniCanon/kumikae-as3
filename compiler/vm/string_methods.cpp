#include "interpreter_internal.h"

#include "number_text.h"
#include "text_methods.h"
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

} // namespace

// StrCall. Each method is the library's function of plain values (text_methods.h, library.h), on this
// frame's registers. split() and the URI functions may run the program or raise: they are handed the values
// themselves, since the registers may move, and what they give is stored where the register then is.
void Interpreter::string_call(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const StringFunction which = static_cast<StringFunction>(p_in.b);
	Slot made;
	if (ir::reads_text_only(which)) {
		if (!text_method(which, Values{ p_registers, list.data(), list.size() }, &characters_, &made)) {
			out_of_memory();
		} else if (p_function.registers[p_in.a] == Kind::Str) {
			assign(Kind::Str, p_registers[p_in.a], made.p);
		} else {
			p_registers[p_in.a] = made;
		}
		return;
	}
	const size_t base = frames_.back().base;
	if (which == StringFunction::Split && is_regexp(p_registers[list[1]])) {
		Vector *pieces = split_by_pattern(static_cast<Object *>(any::pointer(p_registers[list[1]])), text_in(p_registers[list[0]]), to_uint32(p_registers[list[2]].d));
		if (pieces) {
			put(Kind::Obj, stack_[base + p_in.a], of_pointer(pieces));
		}
		return;
	}
	Slot given[3] = {};
	for (size_t i = 0; i < list.size() && i < 3; i++) {
		given[i] = p_registers[list[i]];
	}
	if (text_call(library_, which, given, list.size(), &made)) {
		put(p_function.registers[p_in.a], stack_[base + p_in.a], made);
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
	const bool wide = p_function.registers[list[0]] == Kind::I64;
	const std::optional<std::string> words = number_words(static_cast<NumberFormat>(p_in.b), p_registers[list[0]], wide, p_registers[list[1]].d);
	if (words) {
		assign(Kind::Str, p_registers[p_in.a], text(*words));
	}
}

// toString(radix), toFixed, toExponential or toPrecision of a Number, or of an Int64 (p_wide). Nothing,
// with AS3's RangeError raised, for a radix or a count of digits out of range.
std::optional<std::string> Interpreter::number_words(NumberFormat p_format, Slot p_value, bool p_wide, double p_digits) {
	const double digits = to_integer(p_digits);
	double least = 0, most = MAX_FIXED_DIGITS;
	if (p_format == NumberFormat::Radix) {
		least = 2;
		most = 36;
	} else if (p_format == NumberFormat::Precision) {
		least = 1;
		most = MAX_PRECISION;
	}
	if (!p_wide && (p_format == NumberFormat::Exponential || p_format == NumberFormat::Precision) && !std::isfinite(p_value.d)) {
		// toExponential and toPrecision look at NaN and Infinity before the digits.
		return number_to_string(p_value.d);
	}
	if (digits < least || digits > most) {
		raise(module_.range_error_class, p_format == NumberFormat::Radix ? radix_error(digits) : digits_error());
		return std::nullopt;
	}
	return formatted(p_format, p_value, p_wide, static_cast<int>(digits));
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
