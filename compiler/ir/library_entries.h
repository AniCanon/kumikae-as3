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
};

// With NumText's format (a NumberFormat): the number is an Int64.
constexpr uint32_t NUMBER_TEXT_WIDE = 256;

// The entry for an instruction of a function. It is handed the values the instruction reads, in the
// instruction's order (a register operand, then each of its list); what it gives is the instruction's
// first operand.
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
		default:
			return LibraryEntry{};
	}
}

} // namespace ir
} // namespace kas3
