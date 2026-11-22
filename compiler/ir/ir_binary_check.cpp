#include "ir_binary_check.h"

#include <algorithm>
#include <cmath>

namespace kas3::ir {

namespace {

bool optional_index(uint32_t p_id, size_t p_size) {
	return p_id == NONE || p_id < p_size;
}

bool any_is_code(uint32_t p_code) {
	return p_code >= ANY_IS_INT && p_code <= ANY_IS_OBJECT;
}

bool type_check_valid(const Module &p_module, Kind p_kind, uint32_t p_check) {
	if (p_check == NONE) {
		return true;
	}
	if (p_kind == Kind::Obj) {
		return p_check < p_module.classes.size();
	}
	if (is_engine(p_kind)) {
		return p_check < p_module.engine_names.size();
	}
	return p_kind == Kind::Any;
}

bool terminator(Op p_op) {
	return p_op == Op::Return || p_op == Op::ReturnVoid || p_op == Op::Jump || p_op == Op::Throw || p_op == Op::Rethrow;
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

bool instruction_valid(const Module &p_module, const Function &p_fn, const Instruction &p_in) {
	if (p_in.op == Op::Native) {
		return false;
	}
	const char *shape = op_shape(p_in.op);
	if (!operand_valid(p_module, p_fn, shape[0], p_in.a) || !operand_valid(p_module, p_fn, shape[1], p_in.b) ||
			!operand_valid(p_module, p_fn, shape[2], p_in.c)) {
		return false;
	}
	switch (p_in.op) {
		case Op::CallTypedFunction: {
			const auto scalar = [](Kind p_kind) {
				return p_kind == Kind::I32 || p_kind == Kind::U32 || p_kind == Kind::I64 || p_kind == Kind::U8 ||
						is_floating(p_kind) || p_kind == Kind::Bool || p_kind == Kind::Str;
			};
			const auto &args = p_fn.argument_lists[p_in.c];
			return p_fn.registers[p_in.b] == Kind::Obj && (p_in.a == NONE || scalar(p_fn.registers[p_in.a])) &&
					std::all_of(args.begin(), args.end(), [&](uint32_t p_reg) { return p_reg < p_fn.registers.size() && scalar(p_fn.registers[p_reg]); });
		}
		case Op::ConstU32:
			return p_fn.registers[p_in.a] != Kind::U8 || p_in.b <= UINT8_MAX;
		case Op::ConstF64:
			if (p_fn.registers[p_in.a] == Kind::F32) {
				const double value = p_module.numbers[p_in.b];
				return std::isnan(value) || static_cast<float>(value) == value;
			}
			break;
		case Op::I32ToU8:
			return p_fn.registers[p_in.a] == Kind::U8 && p_fn.registers[p_in.b] == Kind::I32;
		case Op::U8ToI32:
			return p_fn.registers[p_in.a] == Kind::I32 && p_fn.registers[p_in.b] == Kind::U8;
		case Op::U32ToU8:
			return p_fn.registers[p_in.a] == Kind::U8 && p_fn.registers[p_in.b] == Kind::U32;
		case Op::U8ToU32:
			return p_fn.registers[p_in.a] == Kind::U32 && p_fn.registers[p_in.b] == Kind::U8;
		case Op::I64ToF32:
			return p_fn.registers[p_in.a] == Kind::F32 && p_fn.registers[p_in.b] == Kind::I64;
		case Op::F64ToF32:
			return p_fn.registers[p_in.a] == Kind::F32 && p_fn.registers[p_in.b] == Kind::F64;
		case Op::F32ToF64:
			return p_fn.registers[p_in.a] == Kind::F64 && p_fn.registers[p_in.b] == Kind::F32;
		case Op::AddF32: case Op::SubF32: case Op::MulF32: case Op::DivF32: case Op::ModF32:
			return p_fn.registers[p_in.a] == Kind::F32 && p_fn.registers[p_in.b] == Kind::F32 && p_fn.registers[p_in.c] == Kind::F32;
		default:
			break;
	}
	if (p_in.op == Op::FromAny) {
		return type_check_valid(p_module, p_fn.registers[p_in.a], p_in.c);
	}
	if (p_in.op == Op::AnyIs) {
		return p_in.c < p_module.classes.size() || any_is_code(p_in.c);
	}
	if (p_in.op == Op::ClassValue) {
		return p_in.b < p_module.classes.size() || any_is_code(p_in.b);
	}
	if (p_in.op == Op::HostBind) {
		return (p_in.c & BIND_SLOT) != 0 || p_in.c < p_module.functions.size();
	}
	return true;
}

bool function_valid(const Module &p_module, const Function &p_fn) {
	const uint32_t min_params = (p_fn.owner != NONE && !p_fn.is_static ? 1u : 0u) + (p_fn.rest ? 1u : 0u);
	if (p_fn.file >= p_module.files.size() || !optional_index(p_fn.owner, p_module.classes.size()) ||
			!optional_index(p_fn.written_in, p_module.classes.size()) || p_fn.parameters < min_params ||
			p_fn.required > p_fn.parameters || p_fn.parameters > p_fn.registers.size() ||
			(p_fn.rest && p_module.array_class == NONE) || p_fn.code.empty() || !terminator(p_fn.code.back().op)) {
		return false;
	}
	for (uint32_t id : p_fn.parameter_classes) {
		if (!optional_index(id, p_module.classes.size())) {
			return false;
		}
	}
	for (const auto &[at, klass_id] : p_fn.virtual_receivers) {
		if (at >= p_fn.code.size() || klass_id >= p_module.classes.size()) {
			return false;
		}
	}
	for (const Instruction &in : p_fn.code) {
		if (!instruction_valid(p_module, p_fn, in)) {
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
	for (const Local &local : p_fn.locals) {
		if (local.reg >= p_fn.registers.size()) {
			return false;
		}
	}
	for (const Handler &h : p_fn.handlers) {
		const bool valid_type = h.type == NONE || h.type < p_module.classes.size() || (h.type >= ANY_IS_INT && h.type <= ANY_IS_BOOLEAN);
		if (h.start > h.end || h.end > p_fn.code.size() || h.target >= p_fn.code.size() || h.reg >= p_fn.registers.size() ||
				!valid_type) {
			return false;
		}
	}
	return true;
}

bool class_valid(const Module &p_module, const Class &p_class) {
	if (p_class.fixed_array() && (p_class.name.size() == 2 || p_class.element == Kind::Void || p_class.array ||
			p_class.table || p_class.dictionary || p_class.is_interface || p_class.base != NONE || !p_class.fields.empty() ||
			!p_class.methods.empty() || p_class.constructor != NONE)) {
		return false;
	}
	const auto optional_fn = [&](uint32_t id) { return optional_index(id, p_module.functions.size()); };
	const auto valid_fn = [&](uint32_t id) { return id < p_module.functions.size(); };
	if (!optional_fn(p_class.constructor) || !optional_fn(p_class.static_init) || !optional_fn(p_class.to_string) ||
			!type_check_valid(p_module, p_class.element, p_class.element_check) ||
			!std::all_of(p_class.methods.begin(), p_class.methods.end(), valid_fn) ||
			!std::all_of(p_class.interface_methods.begin(), p_class.interface_methods.end(), optional_fn)) {
		return false;
	}
	for (const Field &field : p_class.fields) {
		if (!optional_index(field.klass, p_module.classes.size())) {
			return false;
		}
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
	for (const Global &global : p_module.globals) {
		if (!optional_index(global.klass, p_module.classes.size())) {
			return false;
		}
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
