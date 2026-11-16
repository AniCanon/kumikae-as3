#include "interpreter_internal.h"

#include "number_text.h"
#include "text_methods.h"
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

// The pieces split() gives of a text by a delimiter that is text: the whole of it with none (p_whole), each
// UTF-16 unit for an empty one; at most p_most pieces.
std::vector<std::string> split_text(std::string_view p_self, bool p_whole, std::string_view p_by, uint32_t p_most) {
	const uint32_t length = static_cast<uint32_t>(utf16_length(p_self));
	std::vector<std::string> pieces;
	if (p_whole) {
		pieces.emplace_back(p_self);
	} else if (p_by.empty()) {
		for (uint32_t i = 0; i < length && pieces.size() < p_most; i++) {
			pieces.push_back(units_between(p_self, i, i + 1));
		}
	} else if (length > 0) {
		const uint32_t step = static_cast<uint32_t>(utf16_length(p_by));
		uint32_t from = 0;
		while (pieces.size() < p_most) {
			const int32_t found = find_units(p_self, p_by, from);
			const uint32_t end = found < 0 ? length : static_cast<uint32_t>(found);
			pieces.push_back(units_between(p_self, from, end));
			if (found < 0) {
				break;
			}
			from = end + step;
		}
	} else {
		pieces.emplace_back(); // An empty string with a delimiter: one empty piece.
	}
	if (pieces.size() > p_most) {
		pieces.resize(p_most);
	}
	return pieces;
}

constexpr std::string_view URI_FUNCTIONS[] = { "encodeURI", "encodeURIComponent", "decodeURI", "decodeURIComponent" };

} // namespace

// StrCall. A method that only reads text is the library's function of plain values (text_methods.h), on
// this frame's registers; split() and the URI functions, which may run the program or raise, are here.
void Interpreter::string_call(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const StringFunction which = static_cast<StringFunction>(p_in.b);
	if (which == StringFunction::Split) {
		string_split(p_function, p_in);
		return;
	}
	Slot &out = p_registers[p_in.a];
	if (!reads_text_only(which)) {
		const Str *string = static_cast<const Str *>(p_registers[list[0]].p);
		uri_function(which, string ? string->view() : std::string_view("null"), out);
		return;
	}
	Slot made;
	if (!text_method(which, Values{ p_registers, list.data(), list.size() }, &characters_, &made)) {
		out_of_memory();
	} else if (p_function.registers[p_in.a] == Kind::Str) {
		assign(Kind::Str, out, made.p);
	} else {
		out = made;
	}
}

// escape, unescape and the URI functions. One that can't read or write its text is AS3's URIError #1052.
void Interpreter::uri_function(StringFunction p_which, std::string_view p_text, Slot &r_out) {
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
	const std::vector<std::string> pieces = split_text(string ? string->view() : std::string_view(), whole, by, to_uint32(at(2).d));
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
