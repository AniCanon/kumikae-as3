#include "facts.h"

#include "function_writer.h"
#include "thunks.h"

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

// The operations that are sums, tests and moves, or reach only a vector's elements or a static field.
bool is_plain(Op p_op) {
	switch (p_op) {
		case Op::Stmt:
		case Op::ConstI32:
		case Op::ConstU32:
		case Op::ConstF64:
		case Op::ConstBool:
		case Op::ConstStr:
		case Op::ConstNull:
		case Op::ConstI64:
		case Op::Move:
		case Op::I32ToF64:
		case Op::U32ToF64:
		case Op::BoolToF64:
		case Op::F64ToI32:
		case Op::F64ToU32:
		case Op::I32ToU32:
		case Op::U32ToI32:
		case Op::I32ToBool:
		case Op::U32ToBool:
		case Op::F64ToBool:
		case Op::StrToBool:
		case Op::ObjToBool:
		case Op::AddF64:
		case Op::SubF64:
		case Op::MulF64:
		case Op::DivF64:
		case Op::ModF64:
		case Op::NegF64:
		case Op::BitAnd:
		case Op::BitOr:
		case Op::BitXor:
		case Op::Shl:
		case Op::Shr:
		case Op::UShr:
		case Op::BitNot:
		case Op::LtF64:
		case Op::LeF64:
		case Op::EqF64:
		case Op::EqBool:
		case Op::EqRef:
		case Op::Not:
		case Op::MathUnary:
		case Op::Atan2:
		case Op::Pow:
		case Op::MinF64:
		case Op::MaxF64:
		case Op::Random:
		case Op::Jump:
		case Op::JumpIf:
		case Op::JumpIfNot:
		case Op::GetField:
		case Op::SetField:
		case Op::GetGlobal:
		case Op::SetGlobal:
		case Op::NullCheck:
		case Op::ArgMissing:
		case Op::VecGet:
		case Op::VecSet:
		case Op::VecLength:
		case Op::IsClass:
		case Op::AsClass:
		case Op::Return:
		case Op::ReturnVoid:
			return true;
		default:
			return false;
	}
}

// Whether what a register of the kind held may end as it is let go: a String ends alone.
bool may_end(Kind p_kind) {
	return ir::is_held(p_kind) && p_kind != Kind::Str;
}

// A function's own instructions: the slots they read and write, and whether any may run code.
void look(const ir::Module &p_module, const ir::Function &p_function, FunctionFacts *r_facts) {
	r_facts->opaque = false;
	for (const Instruction &in : p_function.code) {
		r_facts->opaque = r_facts->opaque || may_run_code(p_module, p_function, in);
		r_facts->stores_elements = r_facts->stores_elements || in.op == Op::VecSet;
		if (in.op == Op::GetField) {
			r_facts->reads.insert(in.c);
		} else if (in.op == Op::SetField) {
			r_facts->writes.insert(in.b);
		}
	}
}

// What a function calls is added to what it does itself, until nothing more is.
void follow_calls(const ir::Module &p_module, Facts *r_facts) {
	for (bool changed = true; changed;) {
		changed = false;
		for (size_t i = 0; i < p_module.functions.size(); i++) {
			FunctionFacts &own = r_facts->functions[i];
			for (const Instruction &in : p_module.functions[i].code) {
				if (in.op != Op::Call || own.opaque || in.b == i) {
					continue;
				}
				const FunctionFacts &callee = r_facts->functions[in.b];
				const size_t before = own.reads.size() + own.writes.size() + (own.stores_elements ? 1 : 0);
				own.stores_elements = own.stores_elements || callee.stores_elements;
				own.opaque = callee.opaque;
				own.reads.insert(callee.reads.begin(), callee.reads.end());
				own.writes.insert(callee.writes.begin(), callee.writes.end());
				changed = changed || own.opaque || own.reads.size() + own.writes.size() + (own.stores_elements ? 1 : 0) != before;
			}
		}
	}
}

} // namespace

bool may_run_code(const ir::Module &p_module, const ir::Function &p_function, const Instruction &p_in) {
	return may_run_code(p_module, p_function, p_in, std::vector<bool>());
}

bool may_run_code(const ir::Module &p_module, const ir::Function &p_function, const Instruction &p_in, const std::vector<bool> &p_counts_nothing) {
	if (p_in.op == Op::Call) {
		return may_end(p_module.functions[p_in.b].result); // Kept or not, a result is let go of in the end.
	}
	if (!is_plain(p_in.op)) {
		return true;
	}
	const char first = ir::op_shape(p_in.op)[0];
	if ((first == 'R' || first == 'r') && p_in.a != NONE && !first_is_read(p_in.op) && may_end(p_function.registers[p_in.a]) &&
			!(p_in.a < p_counts_nothing.size() && p_counts_nothing[p_in.a])) {
		return true; // What the register held is let go of.
	}
	// And what a field, a static field or an element held, when another is stored there.
	const uint32_t stored = p_in.op == Op::SetField || p_in.op == Op::VecSet ? p_in.c : p_in.op == Op::SetGlobal ? p_in.b : NONE;
	return stored != NONE && may_end(p_function.registers[stored]);
}

Facts find_facts(const ir::Module &p_module, bool p_keeps_stack) {
	Facts facts;
	facts.functions.resize(p_module.functions.size());
	ThunkTable scratch; // The thunks a function uses are numbered as the functions are really written.
	for (uint32_t i = 0; i < p_module.functions.size(); i++) {
		FunctionWriter writer(p_module, i, scratch, p_keeps_stack, nullptr);
		writer.write();
		facts.functions[i].quiet = !writer.fails();
		look(p_module, p_module.functions[i], &facts.functions[i]);
	}
	follow_calls(p_module, &facts);
	return facts;
}

} // namespace backend_cpp
} // namespace kas3
