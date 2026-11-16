// A function value called with untyped values, as a function of plain values (vm/library.h): the
// arguments as the callee's parameters hold them, and the call itself, which whoever runs the program
// makes by the function's number.
#include "library.h"

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Kind;
using ir::NONE;

namespace {

Slot of_object(Object *p_object) {
	Slot out;
	out.bits = 0;
	out.p = p_object;
	return out;
}

// Calls of a function value seldom pass more: one that does takes its room from the heap.
constexpr size_t USUAL_ARGUMENTS = 8;

} // namespace

bool argument_as(Library &p_library, const Function &p_callee, uint32_t p_register, Slot p_value, Slot *r_out) {
	const Kind kind = p_callee.registers[p_register];
	const uint32_t klass = kind == Kind::Obj && p_register < p_callee.parameter_classes.size() ? p_callee.parameter_classes[p_register] : NONE;
	if (!from_any(p_library, p_value, kind, klass, r_out)) {
		return false;
	}
	const std::string *godot = ir::is_engine(kind) && p_register < p_callee.engine_types.size() ? &p_callee.engine_types[p_register] : nullptr;
	if (godot && !godot->empty() && r_out->p && !p_library.heap.handles.is_class(r_out->p, *godot)) {
		p_library.heap.drop(kind, *r_out);
		p_library.runner.raise(p_library.module().type_error_class, "Error #1034: Type Coercion failed: cannot convert a value to " + *godot + ".");
		return false;
	}
	return true;
}

void raise_argument_count(Library &p_library, const std::string &p_name, size_t p_expected, size_t p_given) {
	const ir::Module &module = p_library.module();
	p_library.runner.raise(module.argument_error_class != NONE ? module.argument_error_class : module.error_class,
			"Error #1063: Argument count mismatch on " + p_name + "(). Expected " + std::to_string(p_expected) + ", got " + std::to_string(p_given) + ".");
}

uint32_t find_method(const Library &p_library, uint32_t p_class, std::string_view p_name) {
	return p_library.heap.find_method(p_class, p_name);
}

std::optional<size_t> function_arguments(Library &p_library, uint32_t p_target, Object *p_first, size_t p_count, const Slot *p_values, Slot *r_values) {
	const Function &callee = p_library.module().functions[p_target];
	const size_t first = callee.is_static ? 0 : 1;
	const size_t most = callee.rest ? SIZE_MAX : callee.parameters - first;
	if (p_count > most || p_count + first < callee.required) {
		raise_argument_count(p_library, callee.name, p_count > most ? most : callee.required - first, p_count);
		return std::nullopt;
	}
	if (first) {
		r_values[0].bits = 0;
		r_values[0].p = p_first;
	}
	const size_t fixed = callee.rest ? callee.parameters - 1 - first : p_count;
	for (size_t i = 0; i < p_count; i++) {
		if (i >= fixed) {
			r_values[first + i] = p_library.heap.share(Kind::Any, p_values[i]); // For the rest parameter, untyped.
			continue;
		}
		r_values[first + i].bits = 0;
		if (!argument_as(p_library, callee, static_cast<uint32_t>(first + i), p_values[i], &r_values[first + i])) {
			let_go_arguments(p_library, callee, r_values, first + i);
			return std::nullopt;
		}
	}
	return first + p_count;
}

void let_go_arguments(Library &p_library, const Function &p_callee, Slot *p_values, size_t p_count) {
	const size_t fixed = p_callee.rest ? p_callee.parameters - 1 : SIZE_MAX;
	for (size_t made = p_callee.is_static ? 0 : 1; made < p_count; made++) {
		p_library.heap.drop(made >= fixed ? Kind::Any : p_callee.registers[made], p_values[made]);
	}
}

Object *function_in(Library &p_library, Kind p_kind, Slot p_value) {
	Object *object = nullptr;
	if (p_kind == Kind::Any) {
		object = held_object(p_value);
	} else if (p_kind == Kind::Obj) {
		object = static_cast<Object *>(p_value.p);
	}
	if (!object || object->klass != p_library.module().function_class) {
		const bool missing = p_kind == Kind::Any ? any::tag(p_value) == AnyTag::Null || any::tag(p_value) == AnyTag::Undefined : !object;
		if (missing) {
			p_library.raise_null();
		} else {
			p_library.runner.raise(p_library.module().type_error_class, "Error #1006: value is not a function.");
		}
		return nullptr;
	}
	return object;
}

bool function_call(Library &p_library, Object *p_function, const Slot *p_arguments, size_t p_count, Slot *r_result) {
	const ir::Module &module = p_library.module();
	if (!function_in(p_library, Kind::Obj, of_object(p_function))) {
		return false;
	}
	const uint32_t target = static_cast<uint32_t>(p_function->fields()[ir::FUNCTION_INDEX].i);
	const Function &callee = module.functions[target];
	Slot usual[USUAL_ARGUMENTS + 1];
	std::vector<Slot> many;
	Slot *values = usual;
	if (p_count > USUAL_ARGUMENTS) {
		many.resize(p_count + 1);
		values = many.data();
	}
	const std::optional<size_t> made =
			function_arguments(p_library, target, static_cast<Object *>(p_function->fields()[ir::FUNCTION_FIRST].p), p_count, p_arguments, values);
	if (!made) {
		return false;
	}
	Slot result;
	result.bits = 0;
	const bool ok = p_library.runner.run(target, values, *made, &result);
	let_go_arguments(p_library, callee, values, *made);
	if (!ok) {
		return false;
	}
	*r_result = callee.result == Kind::Void ? any::undefined() : to_any(p_library, callee.result, result);
	p_library.heap.drop(callee.result, result);
	return true;
}

} // namespace vm
} // namespace kas3
