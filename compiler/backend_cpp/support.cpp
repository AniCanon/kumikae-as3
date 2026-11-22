#include "support.h"

#include "function_writer.h"
#include "patterns.h"

#include <algorithm>

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

using Missing = std::optional<std::string>;

bool fits(char p_wanted, Kind p_kind) {
	switch (p_wanted) {
		case 'n':
			return p_kind == Kind::I32 || p_kind == Kind::U32;
		case 'g':
			return p_kind == Kind::F32;
		case 'f':
			return p_kind == Kind::F64;
		case 'b':
			return p_kind == Kind::Bool;
		case 's':
			return p_kind == Kind::Str;
		case 'o':
			return p_kind == Kind::Obj;
		case 'w':
			return p_kind == Kind::I64;
		default:
			return true;
	}
}

// Whether two functions are called alike: the same parameters, result and way of saying how many
// arguments were given. A call through a method table goes by one of them and reaches any.
bool called_alike(const ir::Function &p_a, const ir::Function &p_b) {
	if (p_a.parameters != p_b.parameters || p_a.result != p_b.result || p_a.rest != p_b.rest || takes_count(p_a) != takes_count(p_b)) {
		return false;
	}
	return std::equal(p_a.registers.begin(), p_a.registers.begin() + p_a.parameters, p_b.registers.begin());
}

// A call's arguments as the callee's parameters hold them, and its result as the register does.
Missing call_fits(const ir::Function &p_function, const Instruction &p_in, const ir::Function &p_callee) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const uint32_t fixed = p_callee.rest ? p_callee.parameters - 1 : p_callee.parameters;
	for (size_t i = 0; i < list.size(); i++) {
		const Kind wanted = i < fixed ? p_callee.registers[i] : Kind::Any;
		if ((i >= fixed && !p_callee.rest) || p_function.registers[list[i]] != wanted) {
			return "a call whose arguments are not of the parameters' kinds";
		}
	}
	if (p_in.a != NONE && p_callee.result != Kind::Void && p_function.registers[p_in.a] != p_callee.result) {
		return "a call whose result is not of the register's kind";
	}
	return std::nullopt;
}

Missing virtual_call(const ir::Module &p_module, const ir::Function &p_function, uint32_t p_pc) {
	const Instruction &in = p_function.code[p_pc];
	uint32_t declared = p_function.owner;
	for (const auto &[at, klass] : p_function.virtual_receivers) {
		if (at == p_pc) {
			declared = klass;
		}
	}
	if (declared == NONE || in.b >= p_module.classes[declared].methods.size() || p_function.argument_lists[in.c].empty()) {
		return "a call through a method table whose class is not known";
	}
	const ir::Function &callee = p_module.functions[p_module.classes[declared].methods[in.b]];
	for (const ir::Class &klass : p_module.classes) {
		if (in.b < klass.methods.size() && !called_alike(callee, p_module.functions[klass.methods[in.b]])) {
			// Only classes below the one that declares the method share its slot's meaning.
			for (uint32_t at = static_cast<uint32_t>(&klass - p_module.classes.data()); at != NONE; at = p_module.classes[at].base) {
				if (at == declared) {
					return "an override whose parameters differ from the method's";
				}
			}
		}
	}
	return call_fits(p_function, in, callee);
}

Missing interface_call(const ir::Module &p_module, const ir::Function &p_function, const Instruction &p_in) {
	if (p_function.argument_lists[p_in.c].empty()) {
		return "an interface call on nothing";
	}
	const ir::Function *first = nullptr;
	for (const ir::Class &klass : p_module.classes) {
		if (p_in.b >= klass.interface_methods.size() || klass.interface_methods[p_in.b] == NONE) {
			continue;
		}
		const ir::Function &callee = p_module.functions[klass.interface_methods[p_in.b]];
		if (first && !called_alike(*first, callee)) {
			return "an interface method whose classes take different parameters";
		}
		first = first ? first : &callee;
	}
	return first ? call_fits(p_function, p_in, *first) : std::nullopt;
}

// getStackTrace() is a read of the Error's field: any read that may be of that field counts. The class of
// the object read is not in the IR, but for the method's own object.
bool reads_stack(const ir::Module &p_module, const ir::Function &p_function, const Instruction &p_in) {
	if (p_in.op != Op::GetField || p_in.c != ir::ERROR_STACK || p_module.error_class == NONE || p_function.registers[p_in.a] != Kind::Str) {
		return false;
	}
	if (p_in.b == 0 && p_function.owner != NONE && !p_function.is_static) {
		for (uint32_t at = p_function.owner; at != NONE; at = p_module.classes[at].base) {
			if (at == p_module.error_class) {
				return true;
			}
		}
		return false;
	}
	return true;
}

Missing instruction(const ir::Module &p_module, const ir::Function &p_function, uint32_t p_pc) {
	const Instruction &in = p_function.code[p_pc];
	const auto kind = [&](uint32_t p_register) { return p_function.registers[p_register]; };
	if (const Pattern *pattern = pattern_of(in.op)) {
		const uint32_t operands[3] = { in.a, in.b, in.c };
		for (int n = 0; n < 3; n++) {
			if (pattern->kinds[n] != '-' && !fits(pattern->kinds[n], kind(operands[n]))) {
				return std::string(ir::op_name(in.op)) + " on a register of another kind";
			}
		}
	}
	switch (in.op) {
		case Op::Move:
			return kind(in.a) == kind(in.b) ? std::nullopt : Missing("a move between registers of two kinds");
		case Op::Return:
			return kind(in.a) == p_function.result ? std::nullopt : Missing("a result of another kind than the function's");
		case Op::GetGlobal:
			return kind(in.a) == p_module.globals[in.b].kind ? std::nullopt : Missing("a static field read as another kind");
		case Op::SetGlobal:
			return kind(in.b) == p_module.globals[in.a].kind ? std::nullopt : Missing("a static field written as another kind");
		case Op::Throw:
		case Op::Rethrow:
			return kind(in.a) == Kind::Any ? std::nullopt : Missing("a throw of a typed register");
		case Op::Call:
			return call_fits(p_function, in, p_module.functions[in.b]);
		case Op::CallVirtual:
			return virtual_call(p_module, p_function, p_pc);
		case Op::CallInterface:
			return interface_call(p_module, p_function, in);
		default:
			return std::nullopt;
	}
}

Missing function(const ir::Module &p_module, const ir::Function &p_function) {
	for (uint32_t pc = 0; pc < p_function.code.size(); pc++) {
		if (Missing missing = instruction(p_module, p_function, pc)) {
			return missing;
		}
	}
	return std::nullopt;
}

} // namespace

std::optional<std::string> first_unsupported(const ir::Module &p_module) {
	for (const ir::Function &each : p_module.functions) {
		if (Missing missing = function(p_module, each)) {
			return missing;
		}
	}
	return std::nullopt;
}

bool reads_error_stack(const ir::Module &p_module) {
	// A property reached by a name the program writes or builds may be getStackTrace too.
	if (std::find(p_module.strings.begin(), p_module.strings.end(), "getStackTrace") != p_module.strings.end()) {
		return true;
	}
	for (const ir::Function &function : p_module.functions) {
		for (const Instruction &in : function.code) {
			if (reads_stack(p_module, function, in)) {
				return true;
			}
		}
	}
	return false;
}

} // namespace backend_cpp
} // namespace kas3
