// What a function's code reaches by calling: said once, for everything that follows calls (what changed
// between two builds, what a constructor chain does, and what the C++ backend knows of a call through a
// method table: which class extends which, and whether a call is on the method's own object).
#pragma once

#include "ir_module.h"

#include <string>
#include <utility>
#include <vector>

namespace kas3 {
namespace ir {

inline bool extends_class(const Module &p_module, uint32_t p_class, uint32_t p_base) {
	for (uint32_t at = p_class; at != NONE && at < p_module.classes.size(); at = p_module.classes[at].base) {
		if (at == p_base) {
			return true;
		}
	}
	return false;
}

// Whether a call's first argument is the object the function itself runs on.
inline bool on_itself(const Function &p_function, const Instruction &p_in) {
	return !p_function.is_static && p_in.c < p_function.argument_lists.size() && !p_function.argument_lists[p_in.c].empty() &&
			p_function.argument_lists[p_in.c][0] == 0;
}

// What the instruction at `p_at` of a function running on an object of class `p_on` may call, each given to
// `p_take(function, the class of the object it runs on, whether that is the object itself)`: by name (a
// constructor too: `new` calls it), on the object itself through its class's method table (an override
// included), and on another object through the class it is declared as (any subclass's override included)
// or an interface (any class's that implements it). A call on an untyped value, or through a function
// value, is not known.
template <class Take>
void each_called(const Module &p_module, const Function &p_function, uint32_t p_at, uint32_t p_on, const Take &p_take) {
	const Instruction &instruction = p_function.code[p_at];
	const bool own = on_itself(p_function, instruction);
	if (instruction.op == Op::Call) {
		const uint32_t owner = instruction.b < p_module.functions.size() ? p_module.functions[instruction.b].owner : NONE;
		p_take(instruction.b, own ? p_on : owner, own);
	} else if (instruction.op == Op::CallVirtual && own) {
		if (p_on < p_module.classes.size() && instruction.b < p_module.classes[p_on].methods.size()) {
			p_take(p_module.classes[p_on].methods[instruction.b], p_on, true);
		}
	} else if (instruction.op == Op::CallVirtual) {
		for (const auto &[at, declared] : p_function.virtual_receivers) {
			for (uint32_t c = 0; c < p_module.classes.size() && at == p_at; c++) {
				if (instruction.b < p_module.classes[c].methods.size() && extends_class(p_module, c, declared)) {
					p_take(p_module.classes[c].methods[instruction.b], c, false);
				}
			}
		}
	} else if (instruction.op == Op::CallInterface) {
		for (uint32_t c = 0; c < p_module.classes.size(); c++) {
			const Class &other = p_module.classes[c];
			if (instruction.b < other.interface_methods.size() && other.interface_methods[instruction.b] != NONE) {
				p_take(other.interface_methods[instruction.b], c, false);
			}
		}
	}
}

// Functions, as text, one after another in the order they are first reached from `p_start` (each a
// function, and the class of the object it runs on), with what each calls (each_called). Each function is
// read with the class of the object it runs on, so that a field or method of that object is named, not
// numbered. `r_reached`, if given, takes the functions in that order.
std::string reached_text(const Module &p_module, std::vector<std::pair<uint32_t, uint32_t>> p_start, std::vector<uint32_t> *r_reached = nullptr);

} // namespace ir
} // namespace kas3
