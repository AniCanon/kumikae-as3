// Function values: made from a method or a function, and called with untyped arguments.
#include "interpreter_internal.h"

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

// A new function value: the function, and what it is called with first (held: a reference of its own). A
// function that takes nothing first has one value, the same each time, as AVM2 gives one for a static
// method: adding and removing it as a listener then match.
Object *Interpreter::make_function(uint32_t p_function, Object *p_first) {
	if (!p_first && module_.functions[p_function].is_static) {
		if (lone_functions_.size() <= p_function) {
			lone_functions_.resize(module_.functions.size(), nullptr);
		}
		if (Object *kept = lone_functions_[p_function]) {
			retain(kept);
			return kept;
		}
	}
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
	if (!p_first && module_.functions[p_function].is_static) {
		retain(made); // Kept until the interpreter goes.
		lone_functions_[p_function] = made;
	}
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
		case Op::FunctionToCallable:
			to_callable(p_in);
			break;
		case Op::FunctionLength: {
			if (Object *value = function_in(p_function.registers[p_in.b], r[p_in.b])) {
				const Function &callee = module_.functions[static_cast<uint32_t>(value->fields()[ir::FUNCTION_INDEX].i)];
				const uint32_t declared = callee.parameters - (callee.is_static ? 0 : 1) - (callee.rest ? 1 : 0);
				set_int32(r[p_in.a], static_cast<int32_t>(declared));
			}
			break;
		}
		case Op::ApplyFunction:
			apply_value(p_function, p_in);
			break;
		case Op::CallbackThis: {
			// A method keeps its own object: Flash will not be told another.
			const Slot value = r[p_in.a];
			const Object *callback = any::tag(value) == AnyTag::Object ? static_cast<const Object *>(any::pointer(value)) : nullptr;
			const bool method = callback && callback->klass == module_.function_class &&
					module_.functions[static_cast<uint32_t>(const_cast<Object *>(callback)->fields()[ir::FUNCTION_INDEX].i)].owner != NONE;
			const AnyTag given = any::tag(r[p_in.b]);
			if (method && given != AnyTag::Null && given != AnyTag::Undefined) {
				raise(module_.type_error_class, "Error #1510: When the callback argument is a method of a class, the optional this argument must be null.");
			}
			break;
		}
		default:
			call_value(p_function, p_in);
			break;
	}
}

// A function value as Godot's Callable: bound to what it is called with first (a method's object, a function
// expression's environment), or to the function value itself for one that takes nothing first. A
// method's is the one its object would give, so that adding and removing a listener match.
void Interpreter::to_callable(const Instruction &p_in) {
	Slot *r = stack_.data() + frames_.back().base;
	Object *value = static_cast<Object *>(r[p_in.b].p);
	Slot made;
	made.bits = 0; // All of it: `{}` on a union clears its first member only.
	if (!value) {
		put(Kind::Val, r[p_in.a], made);
		return;
	}
	const uint32_t target = static_cast<uint32_t>(value->fields()[ir::FUNCTION_INDEX].i);
	const Function &callee = module_.functions[target];
	// A function expression that captured nothing is given nothing first: the value itself goes there.
	Object *first = static_cast<Object *>(value->fields()[ir::FUNCTION_FIRST].p);
	Object *bound = callee.is_static || !first ? value : first;
	const ir::Class &klass = module_.classes[bound->klass];
	void *owner = !callee.is_static && !klass.native_base.empty() && !klass.fields.empty() ? bound->fields()[0].p : nullptr;
	std::string error;
	made.p = host_.source.bind(bound, target, owner, &error);
	if (!made.p) {
		raise(module_.error_class, error.empty() ? "the engine could not take this function" : error);
		return;
	}
	put(Kind::Val, r[p_in.a], made);
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

// What a call of a function value starts with: what it takes first, then each untyped argument as its
// parameter holds it. One left out takes its default; too many is AS3's ArgumentError #1063. False if it
// raised. p_values must not be in the stack: making an argument a String may run the program, which may
// move the stack.
bool Interpreter::function_arguments(Object *p_value, size_t p_count, const Slot *p_values, std::vector<Slot> *r_values) {
	const Function &callee = module_.functions[static_cast<uint32_t>(p_value->fields()[ir::FUNCTION_INDEX].i)];
	const size_t first = callee.is_static ? 0 : 1;
	const size_t most = callee.rest ? SIZE_MAX : callee.parameters - first;
	if (p_count > most || p_count + first < callee.required) {
		raise(module_.argument_error_class != NONE ? module_.argument_error_class : module_.error_class,
				"Error #1063: Argument count mismatch on " + callee.name + "(). Expected " +
						std::to_string(p_count > most ? most : callee.required - first) + ", got " + std::to_string(p_count) + ".");
		return false;
	}
	r_values->assign(first + p_count, Slot());
	if (first) {
		(*r_values)[0].bits = 0;
		(*r_values)[0].p = p_value->fields()[ir::FUNCTION_FIRST].p;
	}
	const size_t fixed = callee.rest ? callee.parameters - 1 - first : p_count;
	for (size_t i = 0; i < p_count; i++) {
		if (i >= fixed) {
			(*r_values)[first + i] = share(Kind::Any, p_values[i]); // For the rest parameter, untyped.
			continue;
		}
		if (!argument_as(callee, static_cast<uint32_t>(first + i), p_values[i], &(*r_values)[first + i])) {
			r_values->resize(first + i);
			let_go_arguments(callee, r_values);
			return false;
		}
	}
	return true;
}

// The arguments function_arguments() made, once the callee has taken references of its own.
void Interpreter::let_go_arguments(const Function &p_callee, std::vector<Slot> *p_values) {
	const size_t fixed = p_callee.rest ? p_callee.parameters - 1 : SIZE_MAX;
	for (size_t made = p_callee.is_static ? 0 : 1; made < p_values->size(); made++) {
		drop(made >= fixed ? Kind::Any : p_callee.registers[made], (*p_values)[made]);
	}
}

// f(a, b): the callee's parameters take the arguments as stores take untyped values. What it gives back is
// untyped.
void Interpreter::call_value(const Function &p_function, const Instruction &p_in) {
	Slot *r = stack_.data() + frames_.back().base;
	if (p_in.a != NONE) {
		put(Kind::Any, r[p_in.a], any::undefined()); // A function that gives nothing gives undefined.
	}
	Object *value = function_in(p_function.registers[p_in.b], r[p_in.b]);
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	if (!value) {
		return;
	}
	// Taken out of the registers first (each still held by its register, which nothing can change while
	// this call is made).
	std::vector<Slot> given(list.size());
	for (size_t i = 0; i < list.size(); i++) {
		given[i] = r[list[i]];
	}
	std::vector<Slot> values;
	if (!function_arguments(value, given.size(), given.data(), &values)) {
		return;
	}
	const uint32_t target = static_cast<uint32_t>(value->fields()[ir::FUNCTION_INDEX].i);
	push(target, values.data(), values.size(), p_in.a); // It takes references of its own.
	let_go_arguments(module_.functions[target], &values);
}

// A function value called from inside an instruction, with untyped arguments: what it gives back, untyped,
// or nothing if it raised (the error is then on its way out, as call_within() leaves it).
std::optional<Slot> Interpreter::call_function_within(Object *p_value, const Slot *p_arguments, size_t p_count) {
	std::vector<Slot> values;
	if (!function_arguments(p_value, p_count, p_arguments, &values)) {
		return std::nullopt;
	}
	const uint32_t target = static_cast<uint32_t>(p_value->fields()[ir::FUNCTION_INDEX].i);
	const Function &callee = module_.functions[target];
	Slot result;
	result.bits = 0;
	const bool ok = call_within(target, values.data(), values.size(), &result);
	let_go_arguments(callee, &values);
	if (!ok) {
		return std::nullopt;
	}
	const Slot untyped = callee.result == Kind::Void ? any::undefined() : to_any(callee.result, result);
	drop(callee.result, result);
	return untyped;
}

// f.apply(thisArg, array): the Array's elements are the arguments; null or undefined is none, and anything
// else is AS3's TypeError #1116.
void Interpreter::apply_value(const Function &p_function, const Instruction &p_in) {
	Slot *r = stack_.data() + frames_.back().base;
	if (p_in.a != NONE) {
		put(Kind::Any, r[p_in.a], any::undefined());
	}
	Object *value = function_in(p_function.registers[p_in.b], r[p_in.b]);
	if (!value) {
		return;
	}
	const Slot list = r[p_in.c];
	const AnyTag tag = any::tag(list);
	const Object *object = tag == AnyTag::Object ? static_cast<const Object *>(any::pointer(list)) : nullptr;
	const Vector *array = object && module_.classes[object->klass].array ? static_cast<const Vector *>(object) : nullptr;
	if (!array && tag != AnyTag::Null && tag != AnyTag::Undefined) {
		raise(module_.type_error_class, "Error #1116: second argument to Function.prototype.apply must be an array.");
		return;
	}
	// The elements, each with a reference of its own: making an argument its type may run the program, which
	// may take them out of the Array.
	std::vector<Slot> given;
	for (uint32_t i = 0; array && i < array->length; i++) {
		given.push_back(share(Kind::Any, array->data[i]));
	}
	std::vector<Slot> values;
	const bool ok = function_arguments(value, given.size(), given.data(), &values);
	for (Slot &each : given) {
		drop_any(each);
	}
	if (!ok) {
		return;
	}
	const uint32_t target = static_cast<uint32_t>(value->fields()[ir::FUNCTION_INDEX].i);
	push(target, values.data(), values.size(), p_in.a);
	let_go_arguments(module_.functions[target], &values);
}

} // namespace vm
} // namespace kas3
