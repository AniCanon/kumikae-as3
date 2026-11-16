// Calls between the program's functions: direct, through the class's method table, and by interface.
#include "function_writer.h"

#include "names.h"

#include <algorithm>

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

// "double (*)(Context *, Object *, double)"
std::string pointer_type(const ir::Function &p_function) {
	const Signature signature = signature_of(p_function);
	std::string out = signature.result + " (*)(";
	for (size_t i = 0; i < signature.parameters.size(); i++) {
		out += (i ? ", " : "") + signature.parameters[i];
	}
	return out + ")";
}

} // namespace

// The function whose parameters and result a call through the method table goes by: the one the class
// that declares the method has in that slot. Every override takes and gives the same (support.cpp).
const ir::Function *FunctionWriter::virtual_callee(const Instruction &p_in) const {
	uint32_t declared = NONE;
	for (const auto &[at, klass] : function_.virtual_receivers) {
		if (at == pc_) {
			declared = klass;
		}
	}
	if (declared == NONE) {
		declared = function_.owner;
	}
	if (declared == NONE || p_in.b >= module_.classes[declared].methods.size()) {
		return nullptr;
	}
	return &module_.functions[module_.classes[declared].methods[p_in.b]];
}

// What the callee is passed after the context: the arguments given, each parameter left out as nothing
// (its default is the callee's to set), a rest parameter's Array, and how many were given.
std::string FunctionWriter::arguments(const ir::Function &p_callee, const std::vector<uint32_t> &p_list) {
	const uint32_t fixed = p_callee.rest ? p_callee.parameters - 1 : p_callee.parameters;
	std::string out = "c";
	for (uint32_t i = 0; i < fixed; i++) {
		const std::string_view taken = parts_of(p_callee, i);
		if (i >= p_list.size()) {
			out += ", " + (taken.empty() ? std::string(zero_of(p_callee.registers[i])) : std::string(taken) + "{}");
		} else if (!taken.empty()) {
			// Held inline by the parameter: the register itself if it holds the same, or its handle's value.
			out += ", " + (parts(p_list[i]) == taken ? register_name(p_list[i]) : "unbox<" + std::string(taken) + ">(c, " + reg(p_list[i]) + ".handle)");
		} else {
			out += ", " + reg(p_list[i]);
		}
	}
	if (p_callee.rest) {
		out += ", rest";
	}
	if (takes_count(p_callee)) {
		out += ", " + std::to_string(std::min<size_t>(p_list.size(), fixed));
	}
	return out;
}

void FunctionWriter::call_function(const Instruction &p_in, const ir::Function &p_callee, const std::string &p_target, bool p_direct) {
	const std::vector<uint32_t> &list = function_.argument_lists[p_in.c];
	const uint32_t fixed = p_callee.rest ? p_callee.parameters - 1 : p_callee.parameters;
	// A function that cannot fail does not look at the depth itself (facts.h): a call that may reach one
	// looks first, and one that surely does has no error to look for afterwards.
	const bool quiet = p_direct && facts_ && facts_->functions[p_in.b].quiet;
	mark();
	line("{");
	if (quiet || !p_direct) {
		line("if (KAS3_UNLIKELY(deep)) { stack_overflow(c); " + fail() + " }");
	}
	if (p_callee.rest) {
		// The arguments past the others, untyped, are the rest parameter's Array: the caller's, let go
		// of once the call is over.
		std::vector<uint32_t> past(list.begin() + std::min<size_t>(list.size(), fixed), list.end());
		std::string slots;
		for (uint32_t each : past) {
			slots += (slots.empty() ? "" : ", ") + reg(each);
		}
		if (!past.empty()) {
			line("const Slot t[] = { " + slots + " };");
		}
		line("Object *const rest = new_array(c, " + (past.empty() ? std::string("nullptr") : "t") + ", " + std::to_string(past.size()) + ");");
		check();
	}
	const std::string call = p_target + "(" + arguments(p_callee, list) + ")";
	const Kind result = p_callee.result;
	if (result == Kind::Void) {
		line(call + ";");
	} else if (p_in.a == NONE) {
		line(ir::is_held(result) ? "release(c, " + call + ");" : call + ";");
	} else {
		line(declared(result, "const v") + " = " + call + ";");
	}
	if (p_callee.rest) {
		line("release(c, rest);");
	}
	if (!quiet) {
		check();
	}
	if (result != Kind::Void && p_in.a != NONE) {
		put(p_in.a, "v");
	}
	line("}");
}

// The function a call's arguments are written for, or null for an instruction that is no call of the
// program's functions (or an interface call nothing answers).
const ir::Function *FunctionWriter::callee_of(const Instruction &p_in) const {
	if (p_in.op == Op::Call) {
		return &module_.functions[p_in.b];
	}
	if (p_in.op == Op::CallVirtual) {
		return virtual_callee(p_in);
	}
	if (p_in.op != Op::CallInterface) {
		return nullptr;
	}
	for (const ir::Class &each : module_.classes) {
		if (p_in.b < each.interface_methods.size() && each.interface_methods[p_in.b] != NONE) {
			return &module_.functions[each.interface_methods[p_in.b]];
		}
	}
	return nullptr;
}

bool FunctionWriter::call(const Instruction &p_in) {
	if (p_in.op == Op::Call) {
		call_function(p_in, module_.functions[p_in.b], function_name(module_, p_in.b), true);
		return true;
	}
	if (p_in.op != Op::CallVirtual && p_in.op != Op::CallInterface) {
		return false;
	}
	const uint32_t receiver = function_.argument_lists[p_in.c][0];
	null_check(receiver);
	const std::string klass = "c->classes[" + reg(receiver) + "->klass]";
	if (p_in.op == Op::CallVirtual) {
		const ir::Function &callee = *virtual_callee(p_in);
		call_function(p_in, callee, "reinterpret_cast<" + pointer_type(callee) + ">(" + klass + ".code[" + std::to_string(p_in.b) + "])", false);
		return true;
	}
	// A class that has no function for the interface's method is the library's to report (TypeError #1006).
	const ir::Function *callee = callee_of(p_in);
	if (!callee) {
		by_thunk(p_in);
		return true;
	}
	line("if (const Code code = " + klass + ".interface_code[" + std::to_string(p_in.b) + "]) {");
	call_function(p_in, *callee, "reinterpret_cast<" + pointer_type(*callee) + ">(code)", false);
	line("} else");
	by_thunk(p_in);
	return true;
}

} // namespace backend_cpp
} // namespace kas3
