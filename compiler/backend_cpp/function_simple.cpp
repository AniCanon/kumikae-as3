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
		case Op::Release:
			if (field_refs_[p_in.a]) {
				line("field_ref" + std::to_string(p_in.a) + ".clear(c, " + reg(p_in.a) + ");");
			} else if (owns(p_in.a)) {
				put(p_in.a, std::string(zero_of(kind(p_in.a))));
			}
			return true;
		case Op::ConstNull:
			put(p_in.a, std::string(zero_of(kind(p_in.a))));
			return true;
		case Op::AnyConst:
			put(p_in.a, p_in.b == 0 ? "empty()" : "any_null(c)");
			return true;
		default:
			return false;
	}
}

// The functions of Math that IEEE 754 gives one answer for, whoever computes it: written as the machine's
// own, where the others are the runtime's (which is the interpreter's, to the last bit).
std::string_view exact_math(uint32_t p_function) {
	switch (static_cast<ir::MathFunction>(p_function)) {
		case ir::MathFunction::Abs:
			return "std::fabs";
		case ir::MathFunction::Ceil:
			return "std::ceil";
		case ir::MathFunction::Floor:
			return "std::floor";
		case ir::MathFunction::Sqrt:
			return "std::sqrt";
		default:
			return {};
	}
}

bool FunctionWriter::simple(const Instruction &p_in) {
	const Pattern *pattern = pattern_of(p_in.op);
	if (!pattern) {
		return false;
	}
	if (const std::string_view exact = p_in.op == ir::Op::MathUnary ? exact_math(p_in.c) : std::string_view(); !exact.empty()) {
		line(reg(p_in.a) + " = " + std::string(exact) + "(" + reg(p_in.b) + ");");
		return true;
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
