#include "ir_binary_check.h"

#include <algorithm>

namespace kas3::ir {

namespace {

bool optional_index(uint32_t p_id, size_t p_size) {
	return p_id == NONE || p_id < p_size;
}

bool operand_valid(const Module &p_module, const Function &p_fn, char p_shape, uint32_t p_value) {
	switch (p_shape) {
		case 'r': return optional_index(p_value, p_fn.registers.size());
		case 'R': return p_value < p_fn.registers.size();
		case 'A': return p_value < p_fn.argument_lists.size();
		case 'L': return p_value < p_fn.code.size();
		case 'C': return p_value < p_module.classes.size();
		case 'F': return p_value < p_module.functions.size();
		case 'G': return p_value < p_module.globals.size();
		case 'M': return p_value < p_module.interface_methods.size();
		case 'E': return p_value < p_module.engine_names.size();
		case 'S': return p_value < p_module.strings.size();
		case 'N': return p_value < p_module.numbers.size();
		case 'W': return p_value < p_module.wides.size();
		default: return true; // Immediates, unused operands and slots whose class is known at runtime.
	}
}

bool function_valid(const Module &p_module, const Function &p_fn) {
	if (p_fn.file >= p_module.files.size() || !optional_index(p_fn.owner, p_module.classes.size()) ||
			!optional_index(p_fn.written_in, p_module.classes.size()) || p_fn.required > p_fn.parameters ||
			p_fn.parameters > p_fn.registers.size()) {
		return false;
	}
	for (const Instruction &in : p_fn.code) {
		const char *shape = op_shape(in.op);
		if (!operand_valid(p_module, p_fn, shape[0], in.a) || !operand_valid(p_module, p_fn, shape[1], in.b) ||
				!operand_valid(p_module, p_fn, shape[2], in.c)) {
			return false;
		}
	}
	for (const auto &args : p_fn.argument_lists) {
		for (uint32_t reg : args) {
			if (reg >= p_fn.registers.size()) {
				return false;
			}
		}
	}
	for (const Handler &h : p_fn.handlers) {
		if (h.start > h.end || h.end > p_fn.code.size() || h.target >= p_fn.code.size() || h.reg >= p_fn.registers.size()) {
			return false;
		}
	}
	return true;
}

bool class_valid(const Module &p_module, const Class &p_class) {
	const auto function = [&](uint32_t id) { return optional_index(id, p_module.functions.size()); };
	if (!function(p_class.constructor) || !function(p_class.static_init) || !function(p_class.to_string) ||
			!std::all_of(p_class.methods.begin(), p_class.methods.end(), function) ||
			!std::all_of(p_class.interface_methods.begin(), p_class.interface_methods.end(), function)) {
		return false;
	}
	for (const auto &entry : p_class.virtuals) {
		if (entry.second >= p_module.functions.size()) {
			return false;
		}
	}
	for (const auto &entry : p_class.named_methods) {
		if (entry.second >= p_class.methods.size()) {
			return false;
		}
	}
	for (uint32_t id : p_class.interfaces) {
		if (id >= p_module.classes.size()) {
			return false;
		}
	}
	return true;
}

} // namespace

bool binary_references_valid(const Module &p_module) {
	if (p_module.lines.size() != p_module.files.size()) {
		return false;
	}
	for (uint32_t id : { p_module.entry_class, p_module.error_class, p_module.type_error_class, p_module.range_error_class,
			 p_module.argument_error_class, p_module.function_class, p_module.class_class, p_module.array_class,
			 p_module.object_class, p_module.dictionary_class, p_module.reference_error_class, p_module.regexp_class,
			 p_module.date_class, p_module.syntax_error_class, p_module.uri_error_class }) {
		if (!optional_index(id, p_module.classes.size())) {
			return false;
		}
	}
	for (uint32_t id : { p_module.equal_operator, p_module.add_operator, p_module.less_operator, p_module.less_equal_operator,
			 p_module.greater_operator, p_module.greater_equal_operator, p_module.subtract_operator, p_module.multiply_operator,
			 p_module.divide_operator, p_module.modulo_operator, p_module.negate_operator }) {
		if (!optional_index(id, p_module.engine_names.size())) {
			return false;
		}
	}
	if (!optional_index(p_module.library_function, p_module.functions.size())) {
		return false;
	}
	for (uint32_t id : p_module.initializers) {
		if (id >= p_module.functions.size()) {
			return false;
		}
	}
	// Mark each base chain once, detecting a cycle even when a class precedes its base in the table.
	std::vector<uint8_t> visited(p_module.classes.size());
	for (size_t i = 0; i < p_module.classes.size(); ++i) {
		if (!class_valid(p_module, p_module.classes[i])) {
			return false;
		}
		uint32_t id = static_cast<uint32_t>(i);
		while (id != NONE && id < visited.size() && visited[id] == 0) {
			visited[id] = 1;
			id = p_module.classes[id].base;
		}
		if (id != NONE && (id >= visited.size() || visited[id] == 1)) {
			return false;
		}
		for (id = static_cast<uint32_t>(i); id != NONE && visited[id] == 1; id = p_module.classes[id].base) {
			visited[id] = 2;
		}
	}
	return std::all_of(p_module.functions.begin(), p_module.functions.end(), [&](const Function &fn) { return function_valid(p_module, fn); });
}

} // namespace kas3::ir
