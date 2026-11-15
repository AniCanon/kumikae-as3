#include "interpreter_internal.h"

#include <cmath>
#include <limits>

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

Slot of_bool(bool p_value) {
	Slot out;
	out.bits = 0;
	out.b = p_value;
	return out;
}

const char *tag_name(AnyTag p_tag) {
	switch (p_tag) {
		case AnyTag::Undefined:
			return "undefined";
		case AnyTag::Null:
			return "null";
		case AnyTag::Boolean:
			return "Boolean";
		case AnyTag::Number:
		case AnyTag::Int:
		case AnyTag::Wide:
			return "Number";
		case AnyTag::String:
			return "String";
		default:
			return "Object";
	}
}

} // namespace

// The value of a register of any kind as an untyped value, with a reference of its own.
Slot Interpreter::to_any(Kind p_kind, Slot p_value) {
	switch (p_kind) {
		case Kind::I32:
			return any::integer(p_value.i);
		case Kind::U32:
			return p_value.u <= static_cast<uint32_t>(INT32_MAX) ? any::integer(static_cast<int32_t>(p_value.u)) : any::number(p_value.u);
		case Kind::F64:
			return any::number(p_value.d);
		case Kind::Bool:
			return any::boolean(p_value.b);
		case Kind::I64: {
			Wide *wide = new_wide(p_value.l);
			return wide ? any::wide(wide) : any::null();
		}
		case Kind::Str:
			retain(static_cast<Str *>(p_value.p));
			return any::string(static_cast<Str *>(p_value.p));
		case Kind::Obj:
			retain(static_cast<Object *>(p_value.p));
			return any::object(static_cast<Object *>(p_value.p));
		case Kind::Val:
			return p_value.p ? any::godot_value(host_.handles.copy(p_value.p)) : any::null();
		case Kind::Ref:
			return p_value.p ? any::godot_object(host_.handles.copy(p_value.p)) : any::null();
		case Kind::Any:
			return share(Kind::Any, p_value);
		default:
			return any::undefined();
	}
}

// What String(x) gives for an untyped value. A script object speaks for itself, which runs the program:
// the value is held while it does. False if that failed; its error is on its way out.
bool Interpreter::any_text(Slot p_value, std::string *r_text) {
	if (const std::optional<std::string> plain = any::text(p_value)) {
		*r_text += *plain;
		return true;
	}
	if (any::tag(p_value) == AnyTag::Object) {
		Object *object = static_cast<Object *>(any::pointer(p_value));
		retain(object);
		const bool said = object_as_text(object, r_text);
		release_object(object);
		return said;
	}
	// A Godot value or object: a handle of its own while the engine speaks, which can run a script.
	void *held = host_.handles.copy(any::pointer(p_value));
	Str *text = held ? host_.handles.text(held) : nullptr;
	*r_text += text ? text->view() : std::string_view("null");
	if (release(text)) {
		free_string(text);
	}
	if (held) {
		host_.handles.drop(held);
	}
	return true;
}

// A store of an untyped value into a register of another kind: AS3's coercion. False if it raised.
bool Interpreter::from_any(Slot p_value, Kind p_kind, uint32_t p_check, Slot *r_out) {
	r_out->bits = 0;
	const AnyTag tag = any::tag(p_value);
	const bool nothing = tag == AnyTag::Undefined || tag == AnyTag::Null;
	switch (p_kind) {
		case Kind::I32:
			r_out->i = to_int32(any::to_number(p_value));
			return true;
		case Kind::U32:
			r_out->u = to_uint32(any::to_number(p_value));
			return true;
		case Kind::F64:
			r_out->d = any::to_number(p_value);
			return true;
		case Kind::Bool:
			r_out->b = any::to_boolean(p_value);
			return true;
		case Kind::I64: {
			if (tag == AnyTag::Wide) {
				r_out->l = static_cast<Wide *>(any::pointer(p_value))->value;
				return true;
			}
			const double number = any::to_number(p_value);
			const std::optional<int64_t> whole = number_to_wide(number);
			if (!whole || static_cast<double>(*whole) != number) {
				raise(module_.range_error_class, number_to_string(number) + " does not fit an Int64.");
				return false;
			}
			r_out->l = *whole;
			return true;
		}
		case Kind::Str: {
			if (nothing) {
				return true;
			}
			if (tag == AnyTag::String) {
				retain(static_cast<Str *>(any::pointer(p_value)));
				r_out->p = any::pointer(p_value);
				return true;
			}
			std::string words;
			if (!any_text(p_value, &words)) {
				return false;
			}
			r_out->p = text(words);
			return true;
		}
		case Kind::Obj: {
			Object *object = tag == AnyTag::Object ? static_cast<Object *>(any::pointer(p_value)) : nullptr;
			if (nothing || (object && (p_check == NONE || is_class(object, p_check)))) {
				retain(object);
				r_out->p = object;
				return true;
			}
			break;
		}
		case Kind::Val:
		case Kind::Ref: {
			const bool fits = p_kind == Kind::Val ? tag == AnyTag::Value
												 : tag == AnyTag::Godot && (p_check == NONE || host_.handles.is_class(any::pointer(p_value), module_.strings[p_check]));
			if (nothing || fits) {
				r_out->p = nothing ? nullptr : host_.handles.copy(any::pointer(p_value));
				return true;
			}
			break;
		}
		case Kind::Any:
			// Into an `Object` (any check given): undefined becomes null. Into a `*`: as it is.
			*r_out = p_check != NONE && tag == AnyTag::Undefined ? any::null() : share(Kind::Any, p_value);
			return true;
		default:
			return true;
	}
	std::string wanted = p_kind == Kind::Obj && p_check != NONE ? module_.classes[p_check].name : "that type";
	if (p_kind != Kind::Obj && p_check != NONE) {
		wanted = module_.strings[p_check];
	}
	raise(module_.type_error_class, std::string("Error #1034: Type Coercion failed: cannot convert a ") + tag_name(tag) + " to " + wanted + ".");
	return false;
}

// `is` on an untyped value: of a class, or a test of the value itself (5.0 is an int).
bool Interpreter::any_is(Slot p_value, uint32_t p_type) const {
	const AnyTag tag = any::tag(p_value);
	const bool numeric = tag == AnyTag::Number || tag == AnyTag::Int || tag == AnyTag::Wide;
	const double number = numeric ? any::to_number(p_value) : 0;
	switch (p_type) {
		case ir::ANY_IS_INT:
			return numeric && number == std::trunc(number) && number >= INT32_MIN && number <= INT32_MAX;
		case ir::ANY_IS_UINT:
			return numeric && number == std::trunc(number) && number >= 0 && number <= UINT32_MAX;
		case ir::ANY_IS_NUMBER:
			return numeric;
		case ir::ANY_IS_STRING:
			return tag == AnyTag::String;
		case ir::ANY_IS_BOOLEAN:
			return tag == AnyTag::Boolean;
		default:
			return tag == AnyTag::Object && is_class(static_cast<const Object *>(any::pointer(p_value)), p_type);
	}
}

// `==` and `===` where the values alone do not say: two Godot values, which Godot compares, or an object
// against a text or a number, compared by its text.
bool Interpreter::any_equal(Slot p_a, Slot p_b, bool p_strict, bool *r_equal) {
	const std::optional<bool> plain = p_strict ? any::strict_equal(p_a, p_b) : any::loose_equal(p_a, p_b);
	if (plain) {
		*r_equal = *plain;
		return true;
	}
	if (any::is_engine(p_a) && any::is_engine(p_b)) {
		const Argument both[2] = { Argument{ any::tag(p_a) == AnyTag::Value ? Kind::Val : Kind::Ref, Slot{} },
			Argument{ any::tag(p_b) == AnyTag::Value ? Kind::Val : Kind::Ref, Slot{} } };
		Argument operands[2] = { both[0], both[1] };
		operands[0].slot.p = any::pointer(p_a);
		operands[1].slot.p = any::pointer(p_b);
		Wanted wanted;
		wanted.kind = Kind::Bool;
		Slot result;
		result.bits = 0;
		std::string error;
		if (!host_.handles.operate("==", operands, 2, wanted, &result, &error)) {
			*r_equal = false; // Godot has no `==` between these two: they are not equal.
			return true;
		}
		*r_equal = result.b;
		return true;
	}
	// An object and a primitive, by `==`: the object's text against the other.
	const bool first_is_object = any::tag(p_a) == AnyTag::Object || any::is_engine(p_a);
	std::string words;
	if (!any_text(first_is_object ? p_a : p_b, &words)) {
		return false;
	}
	Str *as_text = text(words);
	*r_equal = any::loose_equal(any::string(as_text), first_is_object ? p_b : p_a).value_or(false);
	free_string(as_text);
	return true;
}

// An untyped value as a primitive, for `+` and `<`: an object or a Godot value becomes its text.
bool Interpreter::any_primitive(Slot p_value, Slot *r_out) {
	if (any::tag(p_value) != AnyTag::Object && !any::is_engine(p_value)) {
		*r_out = share(Kind::Any, p_value);
		return true;
	}
	std::string words;
	if (!any_text(p_value, &words)) {
		return false;
	}
	*r_out = any::string(text(words));
	return true;
}

// One of the untyped value's operations. Anything that runs the program (an object's text) may move the
// registers: the operands are copied first, and the result is written through the frame's place again.
void Interpreter::any_op(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	const size_t base = frames_.back().base;
	const auto result = [&](Kind p_kind, Slot p_owned) { put(p_kind, stack_[base + p_in.a], p_owned); };
	// Only the operands the operation's shape says are registers are read.
	const Slot b = ir::op_shape(p_in.op)[1] == 'R' ? p_registers[p_in.b] : Slot{};
	const Slot c = ir::op_shape(p_in.op)[2] == 'R' ? p_registers[p_in.c] : Slot{};
	switch (p_in.op) {
		case Op::AnyConst:
			result(Kind::Any, p_in.b ? any::null() : any::undefined());
			break;
		case Op::ToAny:
			result(Kind::Any, to_any(p_function.registers[p_in.b], b));
			break;
		case Op::FromAny: {
			const Kind kind = p_function.registers[p_in.a];
			Slot out;
			const Slot held = share(Kind::Any, b);
			const bool done = from_any(held, kind, p_in.c, &out);
			drop_any(held);
			if (done) {
				result(kind, out);
			}
			break;
		}
		case Op::AnyText: {
			std::string words;
			const Slot held = share(Kind::Any, b);
			const bool done = any_text(held, &words);
			drop_any(held);
			if (done) {
				Slot out;
				out.bits = 0;
				out.p = text(words);
				result(Kind::Str, out);
			}
			break;
		}
		case Op::AnyTypeof: {
			Slot out;
			out.bits = 0;
			out.p = text(any::type_of(b));
			result(Kind::Str, out);
			break;
		}
		case Op::AnyEq:
		case Op::AnyStrictEq: {
			const Slot first = share(Kind::Any, b);
			const Slot second = share(Kind::Any, c);
			bool equal = false;
			const bool done = any_equal(first, second, p_in.op == Op::AnyStrictEq, &equal);
			drop_any(first);
			drop_any(second);
			if (done) {
				result(Kind::Bool, of_bool(equal));
			}
			break;
		}
		case Op::AnyAdd:
		case Op::AnyLt:
		case Op::AnyLe:
			any_arithmetic(p_in, b, c);
			break;
		case Op::AnyIs:
			result(Kind::Bool, of_bool(any_is(b, p_in.c)));
			break;
		default:
			break;
	}
}

// `+`, `<` and `<=` with an untyped operand, as ECMAScript has them: both become primitives; `+` joins
// text if either is text and adds numbers otherwise; `<` compares text if both are text.
void Interpreter::any_arithmetic(const Instruction &p_in, Slot p_b, Slot p_c) {
	const size_t base = frames_.back().base;
	Slot first;
	Slot second;
	if (!any_primitive(p_b, &first)) {
		return;
	}
	if (!any_primitive(p_c, &second)) {
		drop_any(first);
		return;
	}
	const bool texts = any::tag(first) == AnyTag::String || any::tag(second) == AnyTag::String;
	Slot out;
	out.bits = 0;
	if (p_in.op == Op::AnyAdd && texts) {
		std::string joined = *any::text(first) + *any::text(second);
		out = any::string(text(joined));
		put(Kind::Any, stack_[base + p_in.a], out);
	} else if (p_in.op == Op::AnyAdd) {
		put(Kind::Any, stack_[base + p_in.a], any::number(any::to_number(first) + any::to_number(second)));
	} else {
		const bool both_text = any::tag(first) == AnyTag::String && any::tag(second) == AnyTag::String;
		bool answer = false;
		if (both_text) {
			const std::string_view x = static_cast<Str *>(any::pointer(first))->view();
			const std::string_view y = static_cast<Str *>(any::pointer(second))->view();
			answer = p_in.op == Op::AnyLt ? x < y : x <= y;
		} else {
			const double x = any::to_number(first);
			const double y = any::to_number(second);
			answer = p_in.op == Op::AnyLt ? x < y : x <= y;
		}
		put(Kind::Bool, stack_[base + p_in.a], of_bool(answer));
	}
	drop_any(first);
	drop_any(second);
}

} // namespace vm
} // namespace kas3
