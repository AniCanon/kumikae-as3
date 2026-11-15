#include "ir.h"

#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace kas3 {
namespace ir {

namespace {

// Methods a node runs once, when it comes to life or into the tree.
bool runs_once(std::string_view p_function) {
	const size_t slash = p_function.rfind('/');
	const std::string_view method = slash == std::string_view::npos ? std::string_view() : p_function.substr(slash + 1);
	return method == "_ready" || method == "_enter_tree" || method == "_init";
}

// What an instruction does, as text that does not depend on where things sit: a field or a method of
// the object the code runs on is named, not numbered, so that one added before it changes nothing and
// one renamed does. `p_this` is the class of that object; a field or method of any other object keeps
// its number.
std::string instruction_text(const Module &p_module, const Function &p_function, const Instruction &p_in, const Class &p_this) {
	const bool own = !p_function.is_static;
	std::string out = op_name(p_in.op);
	const char *shape = op_shape(p_in.op);
	const uint32_t values[3] = { p_in.a, p_in.b, p_in.c };
	for (int n = 0; n < 3; n++) {
		if (shape[n] == '-') {
			continue;
		}
		const uint32_t value = values[n];
		if (shape[n] == 'D' && own && values[p_in.op == Op::GetField ? 1 : 0] == 0 && value < p_this.fields.size()) {
			out += " ." + p_this.fields[value].name;
		} else if (shape[n] == 'V' && own && value < p_this.methods.size() && p_in.c < p_function.argument_lists.size() &&
				!p_function.argument_lists[p_in.c].empty() && p_function.argument_lists[p_in.c][0] == 0) {
			const uint32_t target = p_this.methods[value];
			out += " #" + (target < p_module.functions.size() ? p_module.functions[target].name : std::string("?"));
		} else {
			out += " " + print_operand(p_module, p_function, shape[n], value);
		}
	}
	return out;
}

// A class's code that runs once for each of its objects, and everything it calls by name or on the
// object itself through the class's method table (an override included), as text: one function after
// another, in the order they are first reached.
std::string run_once_text(const Module &p_module, uint32_t p_class) {
	const Class &klass = p_module.classes[p_class];
	std::vector<uint32_t> pending;
	if (klass.constructor != NONE) {
		pending.push_back(klass.constructor);
	}
	for (uint32_t method : klass.methods) {
		if (method < p_module.functions.size() && runs_once(p_module.functions[method].name)) {
			pending.push_back(method);
		}
	}
	std::unordered_set<uint32_t> seen;
	std::string out;
	for (size_t next = 0; next < pending.size(); next++) {
		const uint32_t index = pending[next];
		if (index >= p_module.functions.size() || !seen.insert(index).second) {
			continue;
		}
		const Function &function = p_module.functions[index];
		out += "function " + function.name + "\n";
		for (const Instruction &instruction : function.code) {
			if (instruction.op == Op::Stmt) {
				continue;
			}
			out += instruction_text(p_module, function, instruction, klass) + "\n";
			if (instruction.op == Op::Call) {
				pending.push_back(instruction.b);
			} else if (instruction.op == Op::CallVirtual && !function.is_static && instruction.b < klass.methods.size() &&
					instruction.c < function.argument_lists.size() && !function.argument_lists[instruction.c].empty() &&
					function.argument_lists[instruction.c][0] == 0) {
				pending.push_back(klass.methods[instruction.b]); // On the object itself: what its class runs.
			}
		}
	}
	return out;
}

} // namespace

std::vector<std::string> run_once_changes(const Module &p_old, const Module &p_new) {
	std::unordered_map<std::string_view, uint32_t> before;
	for (size_t i = 0; i < p_old.classes.size(); i++) {
		before.emplace(p_old.classes[i].name, static_cast<uint32_t>(i));
	}
	std::vector<std::string> changed;
	for (size_t i = 0; i < p_new.classes.size(); i++) {
		const auto found = before.find(p_new.classes[i].name);
		if (found != before.end() && run_once_text(p_old, found->second) != run_once_text(p_new, static_cast<uint32_t>(i))) {
			changed.push_back(p_new.classes[i].name);
		}
	}
	return changed;
}

} // namespace ir
} // namespace kas3
