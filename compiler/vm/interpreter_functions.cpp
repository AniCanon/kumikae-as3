// Function values: made from a method or a function, and called with untyped arguments.
#include "interpreter_internal.h"

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

// A new function value: the function, and what it is called with first (held: a reference of its own).
Object *Interpreter::make_function(uint32_t p_function, Object *p_first) {
	Object *made = module_.function_class == NONE ? nullptr : new_object(module_.function_class, 2);
	if (!made) {
		if (module_.function_class != NONE) {
			out_of_memory();
		}
		return nullptr;
	}
	made->fields()[ir::FUNCTION_INDEX].i = static_cast<int32_t>(p_function);
	retain(p_first);
	made->fields()[ir::FUNCTION_FIRST].p = p_first;
	return made;
}

void Interpreter::function_op(const Function &p_function, const Instruction &p_in) {
	Slot *r = stack_.data() + frames_.back().base;
	switch (p_in.op) {
		case Op::MakeFunction: {
			Object *first = p_in.c == NONE ? nullptr : static_cast<Object *>(r[p_in.c].p);
			if (Object *made = make_function(p_in.b, first)) {
				assign(Kind::Obj, r[p_in.a], made);
			}
			break;
		}
		case Op::MakeMethod: {
			// A method taken from an object: its class's own, bound to it.
			Object *object = static_cast<Object *>(r[p_in.c].p);
			if (!object) {
				raise_null();
				break;
			}
			if (Object *made = make_function(module_.classes[object->klass].methods[p_in.b], object)) {
				assign(Kind::Obj, r[p_in.a], made);
			}
			break;
		}
		default:
			call_value(p_function, p_in);
			break;
	}
}

// Two function values of one method on one object are equal, as AVM2 has it (`f == obj.method`).
bool Interpreter::same_function(const Object *p_a, const Object *p_b) const {
	if (!p_a || !p_b || p_a->klass != module_.function_class || p_b->klass != module_.function_class) {
		return false;
	}
	Object *a = const_cast<Object *>(p_a);
	Object *b = const_cast<Object *>(p_b);
	return a->fields()[ir::FUNCTION_INDEX].i == b->fields()[ir::FUNCTION_INDEX].i &&
			a->fields()[ir::FUNCTION_FIRST].p == b->fields()[ir::FUNCTION_FIRST].p && a->fields()[ir::FUNCTION_FIRST].p;
}

// The function value an untyped or Function register holds, or null with AS3's TypeError #1006.
Object *Interpreter::function_in(Kind p_kind, Slot p_value) {
	Object *object = nullptr;
	if (p_kind == Kind::Any) {
		object = any::tag(p_value) == AnyTag::Object ? static_cast<Object *>(any::pointer(p_value)) : nullptr;
	} else if (p_kind == Kind::Obj) {
		object = static_cast<Object *>(p_value.p);
	}
	if (!object || object->klass != module_.function_class) {
		const bool missing = p_kind == Kind::Any ? any::tag(p_value) == AnyTag::Null || any::tag(p_value) == AnyTag::Undefined : !object;
		if (missing) {
			raise_null();
		} else {
			raise(module_.type_error_class, "Error #1006: value is not a function.");
		}
		return nullptr;
	}
	return object;
}

// One untyped argument as the callee's parameter holds it: AS3's coercion, the class checked for an object
// and the type for one of Godot's. False if it raised.
bool Interpreter::argument_as(const Function &p_callee, uint32_t p_register, Slot p_value, Slot *r_out) {
	const Kind kind = p_callee.registers[p_register];
	const uint32_t klass = kind == Kind::Obj && p_register < p_callee.parameter_classes.size() ? p_callee.parameter_classes[p_register] : NONE;
	if (!from_any(p_value, kind, klass, r_out)) {
		return false;
	}
	const std::string *godot = ir::is_engine(kind) && p_register < p_callee.engine_types.size() ? &p_callee.engine_types[p_register] : nullptr;
	if (godot && !godot->empty() && r_out->p && !host_.handles.is_class(r_out->p, *godot)) {
		drop(kind, *r_out);
		raise(module_.type_error_class, "Error #1034: Type Coercion failed: cannot convert a value to " + *godot + ".");
		return false;
	}
	return true;
}

// f(a, b): the callee's parameters take the arguments as stores take untyped values. One left out takes its
// default; too many is AS3's ArgumentError #1063. What it gives back is untyped.
void Interpreter::call_value(const Function &p_function, const Instruction &p_in) {
	Slot *r = stack_.data() + frames_.back().base;
	if (p_in.a != NONE) {
		put(Kind::Any, r[p_in.a], any::undefined()); // A function that gives nothing gives undefined.
	}
	Object *value = function_in(p_function.registers[p_in.b], r[p_in.b]);
	if (!value) {
		return;
	}
	const uint32_t target = static_cast<uint32_t>(value->fields()[ir::FUNCTION_INDEX].i);
	const Function &callee = module_.functions[target];
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const size_t first = callee.is_static ? 0 : 1;
	const size_t most = callee.parameters - first;
	if (list.size() > most || list.size() + first < callee.required) {
		raise(module_.argument_error_class != NONE ? module_.argument_error_class : module_.error_class,
				"Error #1063: Argument count mismatch on " + callee.name + "(). Expected " +
						std::to_string(list.size() > most ? most : callee.required - first) + ", got " + std::to_string(list.size()) + ".");
		return;
	}
	std::vector<Slot> values(first + list.size());
	if (first) {
		values[0].bits = 0;
		values[0].p = value->fields()[ir::FUNCTION_FIRST].p;
	}
	for (size_t i = 0; i < list.size(); i++) {
		if (!argument_as(callee, static_cast<uint32_t>(first + i), r[list[i]], &values[first + i])) {
			for (size_t made = first; made < first + i; made++) {
				drop(callee.registers[made], values[made]);
			}
			return;
		}
	}
	push(target, values.data(), values.size(), p_in.a); // It takes references of its own.
	for (size_t made = first; made < values.size(); made++) {
		drop(callee.registers[made], values[made]);
	}
}

} // namespace vm
} // namespace kas3
