// Which instructions are carried out whole by one of the library's functions of plain values
// (vm/library.h), and by what name: the interpreter calls that function on a frame's registers, and a back
// end that writes code calls the runtime's function of the same name on the same values, with no
// interpreter between. An instruction with no entry here is the interpreter's own to run.
#pragma once

#include "ir.h"

#include <string_view>

namespace kas3 {
namespace ir {

// The function for an instruction, and the number it is handed with the instruction's values.
struct LibraryEntry {
	std::string_view name; // Empty: the instruction has none.
	uint32_t which = 0;
	// A second number, where the entry needs two: handed after the instruction's values, as a value.
	bool has_more = false;
	uint32_t more = 0;
};

// With NumText's format (a NumberFormat): the number is an Int64.
constexpr uint32_t NUMBER_TEXT_WIDE = 256;

// The entry for an instruction of a function. It is handed the values the instruction reads, in the
// instruction's order (a register operand, then each of its list; the first operand too, where the
// instruction reads it: VecSet's vector); what it gives is the instruction's first operand otherwise.
inline LibraryEntry library_entry(const Function &p_function, const Instruction &p_in) {
	switch (p_in.op) {
		case Op::StrCall:
			// The methods that only read text have an entry of their own (vm/text_methods.h), which can't raise.
			return reads_text_only(static_cast<StringFunction>(p_in.b)) ? LibraryEntry{} : LibraryEntry{ "text_call", p_in.b };
		case Op::I32ToStr:
		case Op::U32ToStr:
		case Op::F64ToStr:
		case Op::BoolToStr:
		case Op::I64ToStr:
			// A number or a Boolean as a String: by the kind of what it is handed.
			return LibraryEntry{ "text_of_value", static_cast<uint32_t>(p_function.registers[p_in.b]) };
		case Op::Concat:
			return LibraryEntry{ "text_joined", 0 };
		case Op::NumText: {
			const bool wide = p_function.registers[p_function.argument_lists[p_in.c][0]] == Kind::I64;
			return LibraryEntry{ "number_text", p_in.b | (wide ? NUMBER_TEXT_WIDE : 0) };
		}
		case Op::New:
			return LibraryEntry{ "object_new", p_in.b };
		case Op::TableNew:
			return LibraryEntry{ "table_new", p_in.b };
		case Op::ToAny:
			return LibraryEntry{ "value_to_any", static_cast<uint32_t>(p_function.registers[p_in.b]) };
		case Op::FromAny:
			// The class or engine name the store checks, and after the value the kind of the place stored to.
			return LibraryEntry{ "any_to_value", p_in.c, true, static_cast<uint32_t>(p_function.registers[p_in.a]) };
		case Op::AnyIs:
			return LibraryEntry{ "any_is_type", p_in.c };
		case Op::VecCall:
			return LibraryEntry{ "vector_call", p_in.b };
		case Op::VecGet:
			// An element by an index held as an int, a uint or a Number: the entry is told which.
			return LibraryEntry{ "vector_get", static_cast<uint32_t>(p_function.registers[p_in.c]) };
		case Op::VecSet:
			return LibraryEntry{ "vector_set", static_cast<uint32_t>(p_function.registers[p_in.b]) };
		case Op::VecLength:
			return LibraryEntry{ "vector_length", 0 };
		case Op::VecSetLength:
			return LibraryEntry{ "vector_set_length", 0 };
		case Op::VecFixed:
			return LibraryEntry{ "vector_fixed", 0 };
		case Op::VecSetFixed:
			return LibraryEntry{ "vector_set_fixed", 0 };
		case Op::VecNew:
			return LibraryEntry{ "vector_new", p_in.b };
		case Op::ArrayNew:
			return LibraryEntry{ "array_new", p_in.b };
		case Op::VecFrom:
			return LibraryEntry{ "vector_from", p_in.c };
		case Op::AnyEq:
		case Op::AnyStrictEq:
		case Op::AnyAdd:
		case Op::AnyLt:
		case Op::AnyLe:
		case Op::AnyGt:
		case Op::AnyGe:
		case Op::AnySub:
		case Op::AnyMul:
		case Op::AnyDiv:
		case Op::AnyMod:
		case Op::AnyNeg:
			return LibraryEntry{ "any_operate", static_cast<uint32_t>(p_in.op.execution) };
		case Op::MakeFunction:
			return LibraryEntry{ "function_make", p_in.b };
		case Op::MakeMethod:
			return LibraryEntry{ "method_make", p_in.b };
		case Op::FunctionLength:
			return LibraryEntry{ "function_length", static_cast<uint32_t>(p_function.registers[p_in.b]) };
		case Op::AnyText:
			return LibraryEntry{ "text_of_any", 0 };
		case Op::ObjToStr:
			return LibraryEntry{ "text_of_object", 0 };
		case Op::CallFunction: {
			// The interpreter empties the result's register before the call: where that register is also
			// one the call reads, the instruction stays the interpreter's own.
			bool read = p_in.a == p_in.b;
			for (const uint32_t each : p_function.argument_lists[p_in.c]) {
				read = read || each == p_in.a;
			}
			return read ? LibraryEntry{} : LibraryEntry{ "function_call", static_cast<uint32_t>(p_function.registers[p_in.b]) };
		}
		default:
			return LibraryEntry{};
	}
}

} // namespace ir
} // namespace kas3
