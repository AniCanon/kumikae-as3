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

const ir::Function *FunctionWriter::virtual_callee(const Instruction &) const {
	return table_signature(module_, function_, pc_);
}

const FunctionFacts *FunctionWriter::called(uint32_t p_pc) const {
	return facts_ ? facts_->called(module_, index_, p_pc) : nullptr;
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

void FunctionWriter::call_function(const Instruction &p_in, const ir::Function &p_callee, const std::vector<Target> &p_targets, bool p_looks_first,
		bool p_may_fail) {
	const std::vector<uint32_t> &list = function_.argument_lists[p_in.c];
	const uint32_t fixed = p_callee.rest ? p_callee.parameters - 1 : p_callee.parameters;
	mark();
	line("{");
	// A function that cannot fail does not look at the depth itself (facts.h): a call that may reach one
	// looks first, and one that surely does has no error to look for afterwards.
	if (p_looks_first) {
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
	const Kind result = p_callee.result;
	const bool kept = result != Kind::Void && p_in.a != NONE;
	const std::string given = "(" + arguments(p_callee, list) + ")";
	if (p_targets.size() > 1) {
		line("const uint32_t k = " + reg(list[0]) + "->klass;");
		if (kept) {
			line(declared(result, "v") + ";");
		}
	}
	for (size_t i = 0; i < p_targets.size(); i++) {
		const std::string call = p_targets[i].function + given;
		const std::string when = p_targets.size() == 1 ? "" : std::string(i ? "else " : "") + (p_targets[i].when.empty() ? "" : "if (" + p_targets[i].when + ") ");
		if (!kept) {
			line(when + (result != Kind::Void && ir::is_held(result) ? "release(c, " + call + ");" : call + ";"));
		} else {
			line(when + (p_targets.size() == 1 ? declared(result, "const v") : std::string("v")) + " = " + call + ";");
		}
	}
	if (p_callee.rest) {
		line("release(c, rest);");
	}
	if (p_may_fail) {
		check();
	}
	if (kept) {
		put(p_in.a, "v");
	}
	line("}");
}

// A call through a class's method table. Where the receiver's class is known (facts.h), the function is one
// of those its classes have in the slot: the one, called by its name, where nothing overrides it; else the
// object's class is compared with those the program makes objects of, each one's function called by its
// name, which the C++ compiler can then see into, and the table is asked for any other class. The call is
// then as those functions are, taken together: no error is looked for where none of them can fail.
void FunctionWriter::call_through_table(const Instruction &p_in) {
	const uint32_t receiver = function_.argument_lists[p_in.c][0];
	const ir::Function &callee = *virtual_callee(p_in);
	const TableFacts *table = facts_ ? facts_->table(module_, index_, pc_) : nullptr;
	null_check(receiver);
	if (table && table->only != NONE) {
		const bool quiet = facts_->functions[table->only].quiet;
		call_function(p_in, callee, { Target{ "", function_name(module_, table->only) } }, quiet, !quiet);
		return;
	}
	const std::string cast = "reinterpret_cast<" + pointer_type(callee) + ">";
	const std::string slot = ".code[" + std::to_string(p_in.b) + "])";
	if (!table || table->made.empty()) {
		call_function(p_in, callee, { Target{ "", cast + "(c->classes[" + reg(receiver) + "->klass]" + slot } }, true, !table || !table->does.quiet);
		return;
	}
	std::vector<Target> targets;
	for (const auto &[klass, held] : table->made) {
		targets.push_back(Target{ "k == " + std::to_string(klass), function_name(module_, held) });
	}
	targets.push_back(Target{ "", cast + "(c->classes[k]" + slot });
	call_function(p_in, callee, targets, true, !table->does.quiet);
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
		const bool quiet = facts_ && facts_->functions[p_in.b].quiet;
		call_function(p_in, module_.functions[p_in.b], { Target{ "", function_name(module_, p_in.b) } }, quiet, !quiet);
		return true;
	}
	if (p_in.op == Op::CallVirtual) {
		call_through_table(p_in);
		return true;
	}
	if (p_in.op != Op::CallInterface) {
		return false;
	}
	const uint32_t receiver = function_.argument_lists[p_in.c][0];
	null_check(receiver);
	const std::string klass = "c->classes[" + reg(receiver) + "->klass]";
	// A class that has no function for the interface's method is the library's to report (TypeError #1006).
	const ir::Function *callee = callee_of(p_in);
	if (!callee) {
		by_thunk(p_in);
		return true;
	}
	line("if (const Code code = " + klass + ".interface_code[" + std::to_string(p_in.b) + "]) {");
	call_function(p_in, *callee, { Target{ "", "reinterpret_cast<" + pointer_type(*callee) + ">(code)" } }, true, true);
	line("} else");
	by_thunk(p_in);
	return true;
}

} // namespace backend_cpp
} // namespace kas3
