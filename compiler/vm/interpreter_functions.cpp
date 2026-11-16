// Function values: made from a method or a function, and called with untyped arguments.
#include "interpreter_internal.h"

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

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
			if (Object *made = vm::make_method(library_, p_in.b, static_cast<Object *>(r[p_in.c].p))) {
				assign(Kind::Obj, r[p_in.a], made);
			}
			break;
		}
		case Op::MakeInterfaceMethod: {
			Object *object = static_cast<Object *>(r[p_in.c].p);
			const uint32_t method = interface_function(object, p_in.b);
			if (method != NONE) {
				if (Object *made = make_function(method, object)) {
					assign(Kind::Obj, r[p_in.a], made);
				}
			}
			break;
		}
		case Op::FunctionToCallable:
			to_callable(p_in);
			break;
		case Op::FunctionLength: {
			if (Object *value = function_in(p_function.registers[p_in.b], r[p_in.b])) {
				set_int32(r[p_in.a], function_length(value));
			}
			break;
		}
		case Op::ApplyFunction:
			apply_value(p_function, p_in);
			break;
		case Op::CallbackThis:
			callback_this(r[p_in.a], r[p_in.b]);
			break;
		default:
			call_value(p_function, p_in);
			break;
	}
}

// Native: the function's body is native code, which whoever holds it runs. The frame is found again by its
// number afterwards: native code may call back in and move the stack.
void Interpreter::native_op(const Instruction &p_in) {
	const size_t index = frames_.size() - 1;
	const Frame &frame = frames_[index];
	Slot result;
	result.bits = 0;
	const bool wanted = p_in.a != NONE;
	const Kind kind = wanted ? module_.functions[frame.function].registers[p_in.a] : Kind::Void;
	if (!native_ || !native_->run_native(frame.function, p_in.b, p_in.c, stack_.data() + frame.base, frame.arguments, wanted ? &result : nullptr)) {
		NativeError error;
		if (!native_ || failing_ || index >= frames_.size() || !native_->hand_over(&error)) {
			raise(module_.error_class, "an error is on its way out of native code");
			return;
		}
		// The error goes on from here as one thrown in this frame, whose line native code has put in.
		pending_ = error.error;
		failing_ = true;
		trace_ = std::move(error.trace);
		trace_lines_ = error.lines;
		thrown_at_ = Site{ std::move(error.function), std::move(error.file), error.line };
		frames_[index].traced = true;
		return;
	}
	if (wanted && index < frames_.size()) {
		put(kind, stack_[frames_[index].base + p_in.a], result);
	}
}

// Arguments: what the caller passed, as an Array, and the running function as its `callee`.
void Interpreter::arguments_array(const Instruction &p_in) {
	const Frame &frame = frames_.back();
	const Function &function = module_.functions[frame.function];
	const uint32_t first = function.is_static ? 0 : 1;
	const uint32_t count = frame.arguments > first ? frame.arguments - first : 0;
	Vector *made = fresh_vector(module_.array_class, count);
	if (!made) {
		return;
	}
	Slot *r = stack_.data() + frame.base;
	for (uint32_t i = 0; i < count; i++) {
		made->data[i] = to_any(function.registers[first + i], r[first + i]);
	}
	Object *callee = make_function(frame.function, first ? static_cast<Object *>(r[0].p) : nullptr);
	Entries *properties = callee ? properties_of(made) : nullptr;
	PropertyKey key;
	key.text = "callee";
	if (!properties || !set_entry(properties, key, any::object(callee))) {
		release_object(callee);
		release_object(made);
		return;
	}
	properties->all()[*entry_of(properties, key)].hidden = true;
	release_object(callee); // The Array holds it now.
	put(Kind::Obj, r[p_in.a], of_object(made));
}

// CallbackThis: a method keeps its own object, so Flash will not be told another (Error #1510).
void Interpreter::callback_this(Slot p_callback, Slot p_this) {
	const Object *callback = any::tag(p_callback) == AnyTag::Object ? static_cast<const Object *>(any::pointer(p_callback)) : nullptr;
	const bool method = callback && callback->klass == module_.function_class &&
			module_.functions[static_cast<uint32_t>(own_field(const_cast<Object *>(callback), ir::FUNCTION_INDEX).i)].owner != NONE;
	const AnyTag given = any::tag(p_this);
	if (method && given != AnyTag::Null && given != AnyTag::Undefined) {
		raise(module_.type_error_class, "Error #1510: When the callback argument is a method of a class, the optional this argument must be null.");
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
	const uint32_t target = static_cast<uint32_t>(own_field(value, ir::FUNCTION_INDEX).i);
	const Function &callee = module_.functions[target];
	// A function expression that captured nothing is given nothing first: the value itself goes there.
	Object *first = static_cast<Object *>(own_field(value, ir::FUNCTION_FIRST).p);
	Object *bound = callee.is_static || !first ? value : first;
	const ir::Class &klass = module_.classes[bound->klass];
	void *owner = !callee.is_static && !klass.native_base.empty() && !klass.fields.empty() ? owner_field(bound).p : nullptr;
	std::string error;
	made.p = host_.source.bind(bound, target, owner, &error);
	if (!made.p) {
		raise(module_.error_class, error.empty() ? "the engine could not take this function" : error);
		return;
	}
	put(Kind::Val, r[p_in.a], made);
}

// What a call of a function value starts with: what it takes first, then each untyped argument as its
// parameter holds it. One left out takes its default; too many is AS3's ArgumentError #1063. False if it
// raised. p_values must not be in the stack: making an argument a String may run the program, which may
// move the stack.
bool Interpreter::function_arguments(Object *p_value, size_t p_count, const Slot *p_values, std::vector<Slot> *r_values) {
	return function_arguments(static_cast<uint32_t>(own_field(p_value, ir::FUNCTION_INDEX).i), static_cast<Object *>(own_field(p_value, ir::FUNCTION_FIRST).p),
			p_count, p_values, r_values);
}

// The same for a function and what it takes first (a method's object), with no function value made.
bool Interpreter::function_arguments(uint32_t p_target, Object *p_first, size_t p_count, const Slot *p_values, std::vector<Slot> *r_values) {
	r_values->assign(p_count + 1, Slot());
	const std::optional<size_t> made = vm::function_arguments(library_, p_target, p_first, p_count, p_values, r_values->data());
	r_values->resize(made.value_or(0));
	return made.has_value();
}

// The arguments function_arguments() made, once the callee has taken references of its own.
void Interpreter::let_go_arguments(const Function &p_callee, std::vector<Slot> *p_values) {
	vm::let_go_arguments(library_, p_callee, p_values->data(), p_values->size());
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
	SpareSlots given(spare_slots_);
	for (const uint32_t each : list) {
		given->push_back(r[each]);
	}
	SpareSlots values(spare_slots_);
	if (!function_arguments(value, given->size(), given->data(), &*values)) {
		return;
	}
	const uint32_t target = static_cast<uint32_t>(own_field(value, ir::FUNCTION_INDEX).i);
	push(target, values->data(), values->size(), p_in.a); // It takes references of its own.
	let_go_arguments(module_.functions[target], &*values);
}

// A function value called from inside an instruction, with untyped arguments: what it gives back, untyped,
// or nothing if it raised (the error is then on its way out, as call_within() leaves it).
std::optional<Slot> Interpreter::call_function_within(Object *p_value, const Slot *p_arguments, size_t p_count) {
	Slot given = any::undefined();
	return vm::function_call(library_, p_value, p_arguments, p_count, &given) ? std::optional<Slot>(given) : std::nullopt;
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
	SpareSlots given(spare_slots_);
	for (uint32_t i = 0; array && i < array->length; i++) {
		given->push_back(share(Kind::Any, array->data[i]));
	}
	SpareSlots values(spare_slots_);
	const bool ok = function_arguments(value, given->size(), given->data(), &*values);
	for (Slot &each : *given) {
		drop_any(each);
	}
	if (!ok) {
		return;
	}
	const uint32_t target = static_cast<uint32_t>(own_field(value, ir::FUNCTION_INDEX).i);
	push(target, values->data(), values->size(), p_in.a);
	let_go_arguments(module_.functions[target], &*values);
}

} // namespace vm
} // namespace kas3
