// The fast paths that are not inline (runtime/fast.h): typed and untyped values made of one another where
// nothing needs converting, a function value called with no interpreter between, and an object's own
// toString(). Each follows the interpreter's rule for the cases it takes (vm/interpreter_any.cpp,
// vm/interpreter_functions.cpp) and leaves every other case to it.
#include "fast.h"

#include "native_host.h"
#include "table.h"

#include <cmath>

namespace kas3 {
namespace rt {

using ir::Kind;
using vm::AnyTag;

Str *text_of(int32_t p_value) {
	return vm::new_string(std::to_string(p_value));
}
Str *text_of(uint32_t p_value) {
	return vm::new_string(std::to_string(p_value));
}
Str *text_of(int64_t p_value) {
	return vm::new_string(std::to_string(p_value));
}
Str *text_of(double p_value) {
	return vm::new_string(vm::number_to_string(p_value));
}
Str *text_of(bool p_value) {
	return vm::new_string(p_value ? "true" : "false");
}

bool number_to_any(double p_value, Slot *r_any) {
	if (p_value != std::trunc(p_value)) {
		*r_any = vm::any::number(p_value); // Not-a-number too: it is not equal to itself.
		return true;
	}
	constexpr double EXACT = 9007199254740992.0; // 2^53: past it a Number is not every whole number.
	if (std::fabs(p_value) > EXACT || (p_value == 0 && std::signbit(p_value))) {
		*r_any = vm::any::number(p_value);
		return true;
	}
	if (p_value >= INT32_MIN && p_value <= INT32_MAX) {
		*r_any = vm::any::integer(static_cast<int32_t>(p_value));
		return true;
	}
	// A whole number past an int is held as an Int64, in a box.
	vm::Wide *wide = vm::new_wide(static_cast<int64_t>(p_value));
	*r_any = vm::any::wide(wide);
	return wide != nullptr;
}

bool from_any(const Context *c, uint8_t p_kind, uint32_t p_class, Slot p_any, Slot *r_value) {
	const AnyTag tag = vm::any::tag(p_any);
	const bool number = tag == AnyTag::Number || tag == AnyTag::Int;
	const bool nothing = tag == AnyTag::Null || tag == AnyTag::Undefined;
	*r_value = empty();
	switch (static_cast<Kind>(p_kind)) {
		case Kind::I32:
			if (number) {
				r_value->i = tag == AnyTag::Int ? vm::any::to_int_payload(p_any) : vm::to_int32(vm::any::to_number_payload(p_any));
			}
			return number;
		case Kind::U32:
			if (number) {
				r_value->u = vm::to_uint32(vm::any::to_number(p_any));
			}
			return number;
		case Kind::F64:
			if (number) {
				r_value->d = vm::any::to_number(p_any);
			}
			return number;
		case Kind::Bool:
			r_value->b = vm::any::to_bool_payload(p_any);
			return tag == AnyTag::Boolean;
		case Kind::Str:
			r_value->p = tag == AnyTag::String ? vm::any::pointer(p_any) : nullptr;
			return tag == AnyTag::String || nothing;
		case Kind::Obj: {
			Object *object = tag == AnyTag::Object ? static_cast<Object *>(vm::any::pointer(p_any)) : nullptr;
			r_value->p = object;
			return nothing || (object && (p_class == NONE || is_class(c, object, p_class)));
		}
		case Kind::Any:
			// Into an `Object` (a class given): undefined becomes null. Into a `*`: as it is.
			*r_value = p_class != NONE && tag == AnyTag::Undefined ? vm::any::null() : p_any;
			return true;
		default:
			return false;
	}
}

bool has_members(Slot p_any) {
	const AnyTag tag = vm::any::tag(p_any);
	return tag != AnyTag::Null && tag != AnyTag::Undefined;
}

Object *function_of(Slot p_any) {
	return vm::any::tag(p_any) == AnyTag::Object ? static_cast<Object *>(vm::any::pointer(p_any)) : nullptr;
}

namespace {

constexpr uint32_t MOST_PARAMETERS = 16; // A function with more is called by the library.

// A function's result as an untyped value, which takes over its reference. False if the library's
// boxing of it failed: the error is on its way out.
bool result_to_any(Context *c, Kind p_kind, Slot p_result, Slot *r_any) {
	switch (p_kind) {
		case Kind::Void:
			*r_any = vm::any::undefined();
			return true;
		case Kind::I32:
			*r_any = vm::any::integer(p_result.i);
			return true;
		case Kind::U32:
			*r_any = to_any(c, p_result.u);
			return true;
		case Kind::Bool:
			*r_any = vm::any::boolean(p_result.b);
			return true;
		case Kind::Str:
			*r_any = vm::any::string(static_cast<Str *>(p_result.p));
			return true;
		case Kind::Obj:
			*r_any = vm::any::object(static_cast<Object *>(p_result.p));
			return true;
		case Kind::Any:
			*r_any = p_result;
			return true;
		case Kind::F64:
			if (number_to_any(p_result.d, r_any)) {
				return true;
			}
			*r_any = c->host->builtin(Builtin::NumberToAny, &p_result);
			return !c->failing;
		default:
			*r_any = c->host->builtin(Builtin::WideToAny, &p_result);
			return !c->failing;
	}
}

} // namespace

Object *arguments_of(Context *c, uint32_t p_function, const Slot *p_parameters, uint32_t p_given, Object *p_callee) {
	const FunctionInfo &function = c->program->functions[p_function];
	const uint32_t first = function.is_static ? 0 : 1;
	const uint32_t count = p_given > first ? p_given - first : 0;
	std::vector<Slot> values(count, empty());
	for (uint32_t i = 0; i < count && !c->failing; i++) {
		// Each as an untyped value with a reference of its own: result_to_any() takes over the one taken here.
		const Kind kind = static_cast<Kind>(function.kinds[first + i]);
		const Slot held = p_parameters[first + i];
		if (kind == Kind::Str) {
			retain(c, static_cast<Str *>(held.p));
		} else if (kind == Kind::Obj) {
			retain(c, static_cast<Object *>(held.p));
		}
		result_to_any(c, kind, kind == Kind::Any ? shared(c, held) : held, &values[i]);
	}
	Object *made = c->failing ? nullptr : new_array(c, values.data(), count);
	for (Slot each : values) {
		release(c, each);
	}
	vm::Entries *properties = made ? vm::properties_of(static_cast<Vector *>(made)) : nullptr;
	Str *name = properties ? vm::new_string("callee") : nullptr;
	if (!name) {
		release(c, p_callee);
		release(c, made);
		if (!c->failing) {
			out_of_memory(c);
		}
		return nullptr;
	}
	properties->add(vm::any::string(name), vm::any::object(p_callee), true);
	return made;
}

bool call_value(Context *c, Object *p_function, const Slot *p_arguments, uint32_t p_count, Slot *r_result) {
	const Program &program = *c->program;
	if (!p_function || p_function->klass != program.known.function) {
		return false;
	}
	const FunctionInfo &callee = program.functions[static_cast<uint32_t>(field<int32_t>(p_function, ir::FUNCTION_INDEX))];
	const uint32_t first = callee.is_static ? 0 : 1;
	if (callee.rest || callee.parameters > MOST_PARAMETERS || p_count + first > callee.parameters || p_count + first < callee.required) {
		return false;
	}
	Slot values[MOST_PARAMETERS];
	for (uint32_t i = 0; i < callee.parameters; i++) {
		values[i] = empty();
	}
	if (first) {
		values[0].p = field<Object *>(p_function, ir::FUNCTION_FIRST);
	}
	for (uint32_t i = 0; i < p_count; i++) {
		const uint32_t at = first + i;
		// A parameter's class is checked for an object only, as a call through a value checks it.
		const uint32_t klass = static_cast<Kind>(callee.kinds[at]) == Kind::Obj ? callee.classes[at] : NONE;
		if (!from_any(c, callee.kinds[at], klass, p_arguments[i], &values[at])) {
			return false;
		}
	}
	// The function value and its arguments stay the caller's, whose registers hold them throughout.
	Slot result = empty();
	callee.entry(c, values, first + p_count, &result);
	if (!c->failing) {
		result_to_any(c, static_cast<Kind>(callee.result), result, r_result);
	}
	return true;
}

bool object_text(Context *c, Object *p_object, Str **r_text) {
	if (!p_object) {
		return false;
	}
	const ClassInfo &klass = c->classes[p_object->klass];
	if (klass.element != static_cast<uint8_t>(Kind::Void) || klass.table || klass.to_string == NONE) {
		return false;
	}
	const FunctionInfo &own = c->program->functions[klass.to_string];
	if (static_cast<Kind>(own.result) != Kind::Str || own.parameters != 1) {
		return false;
	}
	*r_text = reinterpret_cast<Str *(*)(Context *, Object *)>(own.direct)(c, p_object);
	return true;
}

bool any_text(Context *c, Slot p_any, Str **r_text) {
	const AnyTag tag = vm::any::tag(p_any);
	if (tag == AnyTag::String) {
		*r_text = static_cast<Str *>(vm::any::pointer(p_any));
		retain(c, *r_text);
		return true;
	}
	if (tag != AnyTag::Object || !object_text(c, static_cast<Object *>(vm::any::pointer(p_any)), r_text)) {
		return false;
	}
	if (!c->failing && !*r_text) {
		*r_text = c->null_text; // A toString() that gives null reads "null".
		retain(c, *r_text);
	}
	return true;
}

} // namespace rt
} // namespace kas3
