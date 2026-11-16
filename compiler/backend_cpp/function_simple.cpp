// Constants, and the operations that are one C++ expression: arithmetic, conversions, comparisons. Each
// says what the interpreter's loop does for it (vm/interpreter_run.cpp), in the runtime's words.
#include "function_writer.h"

#include "names.h"
#include "patterns.h"

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::Op;

bool FunctionWriter::constant(const Instruction &p_in) {
	switch (p_in.op) {
		case Op::ConstI32:
			// The least int is written as the int before it, less one: `-2147483648` alone is too big.
			line(reg(p_in.a) + " = " + (p_in.b == 0x80000000u ? std::string("INT32_MIN") : std::to_string(static_cast<int32_t>(p_in.b))) + ";");
			return true;
		case Op::ConstU32:
			line(reg(p_in.a) + " = " + std::to_string(p_in.b) + "u;");
			return true;
		case Op::ConstF64:
			line(reg(p_in.a) + " = " + number_literal(module_.numbers[p_in.b]) + ";");
			return true;
		case Op::ConstI64:
			line(reg(p_in.a) + " = " + wide_literal(module_.wides[p_in.b]) + ";");
			return true;
		case Op::ConstBool:
			line(reg(p_in.a) + " = " + (p_in.b != 0 ? "true" : "false") + ";");
			return true;
		case Op::ConstStr:
			share(p_in.a, "c->strings[" + std::to_string(p_in.b) + "]");
			return true;
		case Op::ConstNull:
			put(p_in.a, std::string(zero_of(kind(p_in.a))));
			return true;
		case Op::AnyConst:
			put(p_in.a, p_in.b == 0 ? "empty()" : "kas3::vm::any::null()");
			return true;
		default:
			return false;
	}
}

bool FunctionWriter::simple(const Instruction &p_in) {
	const Pattern *pattern = pattern_of(p_in.op);
	if (!pattern) {
		return false;
	}
	const uint32_t operands[3] = { p_in.a, p_in.b, p_in.c };
	std::string out;
	const std::string_view text = pattern->text;
	for (size_t i = 0; i < text.size(); i++) {
		const bool last = i + 1 >= text.size();
		const char next = last ? '\0' : text[i + 1];
		if (text[i] == '$' && next >= 'a' && next <= 'c') {
			out += reg(operands[next - 'a']);
			i++;
		} else if (text[i] == '$' && next == 'A') {
			out += type_of(kind(p_in.a)); // What the first register holds: the expression is cast to it.
			i++;
		} else if (text[i] == '#' && next == 'c') {
			out += std::to_string(p_in.c);
			i++;
		} else {
			out += text[i];
		}
	}
	line(out);
	return true;
}

} // namespace backend_cpp
} // namespace kas3
