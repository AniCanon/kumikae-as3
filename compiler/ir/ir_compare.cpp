#include "ir.h"

#include "ir_reach.h"

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

} // namespace

std::string reached_text(const Module &p_module, std::vector<std::pair<uint32_t, uint32_t>> p_start, std::vector<uint32_t> *r_reached) {
	auto &pending = p_start; // A function, and the class of the object it runs on.
	std::unordered_set<uint32_t> seen;
	std::string out;
	for (size_t next = 0; next < pending.size(); next++) {
		const uint32_t index = pending[next].first;
		const uint32_t on = pending[next].second;
		if (index >= p_module.functions.size() || !seen.insert(index).second) {
			continue;
		}
		if (r_reached) {
			r_reached->push_back(index);
		}
		const Function &function = p_module.functions[index];
		static const Class none; // A function with no object of a class: nothing is named for one.
		const Class &klass = on < p_module.classes.size() ? p_module.classes[on] : none;
		out += "function " + function.name + "\n";
		for (uint32_t at = 0; at < function.code.size(); at++) {
			if (function.code[at].op == Op::Stmt) {
				continue;
			}
			out += instruction_text(p_module, function, function.code[at], klass) + "\n";
			each_called(p_module, function, at, on, [&](uint32_t p_called, uint32_t p_on, bool) { pending.emplace_back(p_called, p_on); });
		}
	}
	return out;
}

namespace {

// A class's code that runs once for each of its objects (its constructor, which gives fields their first
// values, and `_ready`, `_enter_tree`, `_init`), with what it reaches.
std::string run_once_text(const Module &p_module, uint32_t p_class) {
	const Class &klass = p_module.classes[p_class];
	std::vector<std::pair<uint32_t, uint32_t>> start;
	if (klass.constructor != NONE) {
		start.emplace_back(klass.constructor, p_class);
	}
	for (uint32_t method : klass.methods) {
		if (method < p_module.functions.size() && runs_once(p_module.functions[method].name)) {
			start.emplace_back(method, p_class);
		}
	}
	return reached_text(p_module, std::move(start));
}

// What gives a class's static fields their first values, with what it reaches.
std::string static_text(const Module &p_module, uint32_t p_class) {
	const uint32_t start = p_module.classes[p_class].static_init;
	return start == NONE ? std::string() : reached_text(p_module, { { start, p_class } });
}

// The classes in both builds whose text, by `p_text`, differs.
std::vector<std::string> changed_classes(const Module &p_old, const Module &p_new, std::string (*p_text)(const Module &, uint32_t)) {
	std::unordered_map<std::string_view, uint32_t> before;
	for (size_t i = 0; i < p_old.classes.size(); i++) {
		before.emplace(p_old.classes[i].name, static_cast<uint32_t>(i));
	}
	std::vector<std::string> changed;
	for (size_t i = 0; i < p_new.classes.size(); i++) {
		const auto found = before.find(p_new.classes[i].name);
		if (found != before.end() && p_text(p_old, found->second) != p_text(p_new, static_cast<uint32_t>(i))) {
			changed.push_back(p_new.classes[i].name);
		}
	}
	return changed;
}

} // namespace

std::vector<std::string> run_once_changes(const Module &p_old, const Module &p_new) {
	return changed_classes(p_old, p_new, &run_once_text);
}

std::vector<std::string> static_changes(const Module &p_old, const Module &p_new) {
	return changed_classes(p_old, p_new, &static_text);
}

} // namespace ir
} // namespace kas3

