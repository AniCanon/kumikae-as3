// Which instructions are carried out whole by one of the library's functions of plain values
// (vm/library.h), and by what name: the interpreter calls that function on a frame's registers, and a back
// end that writes code calls the runtime's function of the same name on the same values, with no
// interpreter between. An instruction with no entry here is the interpreter's own to run.
#pragma once

#include "ir.h"

#include <string_view>

namespace kas3 {
namespace ir {

// The function for an instruction (by its operation and its second operand), or nothing. It is handed the
// values the instruction reads, in the instruction's order (a register operand, then each of its list),
// and its second operand where that is a number; what it gives is the instruction's first operand.
constexpr std::string_view library_entry(Op p_op, uint32_t p_which) {
	switch (p_op) {
		case Op::StrCall:
			// The methods that only read text have an entry of their own (vm/text_methods.h), which can't raise.
			return reads_text_only(static_cast<StringFunction>(p_which)) ? std::string_view() : std::string_view("text_call");
		default:
			return std::string_view();
	}
}

} // namespace ir
} // namespace kas3
