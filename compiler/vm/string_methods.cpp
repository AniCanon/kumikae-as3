#include "interpreter_internal.h"

#include "text_methods.h"
#include "text_units.h"


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

// NumText: toString(radix), toFixed, toExponential or toPrecision, by the library's function.
void Interpreter::number_text(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const Slot given[2] = { p_registers[list[0]], p_registers[list[1]] };
	Slot made;
	if (vm::number_text(library_, static_cast<NumberFormat>(p_in.b), p_function.registers[list[0]] == Kind::I64, given, 2, &made)) {
		assign(Kind::Str, p_registers[p_in.a], made.p);
	}
}

bool Interpreter::any_null_check(Slot p_value) {
	const AnyTag tag = any::tag(present_or_null(p_value));
	if (tag == AnyTag::Undefined) {
		raise(module_.type_error_class, "Error #1010: A term is undefined and has no properties.");
	} else if (tag == AnyTag::Null) {
		raise_null();
	} else {
		return true;
	}
	return false;
}

} // namespace vm
} // namespace kas3
