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

// A class's code that runs once, and everything it calls by name or through the class's own method
// table, as text: one function after another, in the order they are first reached.
std::string run_once_text(const Module &p_module, uint32_t p_class) {
	const Class &klass = p_module.classes[p_class];
	std::vector<uint32_t> pending;
	for (uint32_t start : { klass.constructor, klass.static_init }) {
		if (start != NONE) {
			pending.push_back(start);
		}
	}
	for (size_t i = 0; i < p_module.functions.size(); i++) {
		if (p_module.functions[i].owner == p_class && runs_once(p_module.functions[i].name)) {
			pending.push_back(static_cast<uint32_t>(i));
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
		out += print(p_module, function);
		for (const Instruction &instruction : function.code) {
			if (instruction.op == Op::Call) {
				pending.push_back(instruction.b);
			} else if (instruction.op == Op::CallVirtual && instruction.b < klass.methods.size()) {
				pending.push_back(klass.methods[instruction.b]);
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
