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
			return any_number(p_value.d);
		case Kind::Bool:
			return any::boolean(p_value.b);
		case Kind::I64: {
			Wide *wide = new_wide(p_value.l);
			if (!wide) {
				out_of_memory();
				return any::undefined();
			}
			return any::wide(wide);
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
		if (!text) {
			out_of_memory();
			return false;
		}
	}
	return true;
}

// A store of an untyped value into a register of another kind: AS3's coercion. False if it raised.
bool Interpreter::from_any(Slot p_value, Kind p_kind, uint32_t p_check, Slot *r_out) {
	r_out->bits = 0;
	const bool numeric = p_kind == Kind::I32 || p_kind == Kind::U32 || p_kind == Kind::F64 || p_kind == Kind::I64;
	if (numeric && (any::tag(p_value) == AnyTag::Object || any::is_engine(p_value))) {
		// An object as a number: its text, then that as a number (ECMAScript's ToPrimitive).
		Slot primitive;
		if (!any_primitive(p_value, &primitive)) {
			return false;
		}
		const bool done = from_any(primitive, p_kind, p_check, r_out);
		drop_any(primitive);
		return done;
	}
	if (p_kind == Kind::Bool && any::is_engine(p_value) && !host_.handles.present(any::pointer(p_value))) {
		return true; // A Godot object that has been freed is null, so false.
	}
	const AnyTag tag = any::tag(p_value);
	const bool nothing = tag == AnyTag::Undefined || tag == AnyTag::Null;
	switch (p_kind) {
		// A large integer wraps (the owner's ruling): an Int64 by its own low bits, not a rounded Number's.
		case Kind::I32:
			r_out->i = tag == AnyTag::Wide ? (int32_t)(uint32_t)(uint64_t) static_cast<Wide *>(any::pointer(p_value))->value
										   : to_int32(any::to_number(p_value));
			return true;
		case Kind::U32:
			r_out->u = tag == AnyTag::Wide ? (uint32_t)(uint64_t) static_cast<Wide *>(any::pointer(p_value))->value
										   : to_uint32(any::to_number(p_value));
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
			if (p_check != NONE && engine_array(p_value, p_check)) {
				// One of Godot's Arrays where an Array is wanted: a new Array of its elements.
				Vector *made = array_of_engine(any::pointer(p_value), p_check);
				r_out->p = made;
				return made != nullptr;
			}
			break;
		}
		case Kind::Val:
		case Kind::Ref: {
			const bool fits = (p_kind == Kind::Val ? tag == AnyTag::Value : tag == AnyTag::Godot) &&
					(p_check == NONE || host_.handles.is_class(any::pointer(p_value), module_.strings[p_check]));
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
	// Flash's words: the value as text where it has one, else its type, and the type it was to be.
	std::string wanted = ir::kind_name(p_kind);
	if (p_check != NONE) {
		wanted = p_kind == Kind::Obj ? module_.classes[p_check].name : module_.strings[p_check];
	}
	const std::optional<std::string> shown = any::text(p_value);
	raise(module_.type_error_class, "Error #1034: Type Coercion failed: cannot convert " + (shown ? *shown : std::string(tag_name(tag))) +
			" to " + wanted + ".");
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
			return (tag == AnyTag::Object && is_class(static_cast<const Object *>(any::pointer(p_value)), p_type)) || engine_array(p_value, p_type);
	}
}

// Whether a value is one of Godot's Arrays and the class is Array: AS3 takes one as an Array of its own.
bool Interpreter::engine_array(Slot p_value, uint32_t p_class) const {
	return any::tag(p_value) == AnyTag::Value && p_class < module_.classes.size() && module_.classes[p_class].array &&
			host_.handles.is_class(any::pointer(p_value), "Array");
}

Object *Interpreter::copy_engine_array(void *p_array, uint32_t p_class) {
	Vector *made = array_of_engine(p_array, p_class);
	if (!made) {
		report();
	}
	return made;
}

// A new Array of the elements of one of Godot's Arrays, each untyped: a copy, as a Vector given to Godot
// is copied. Null, with the error raised, if the engine could not give them.
Vector *Interpreter::array_of_engine(void *p_array, uint32_t p_class) {
	std::string error;
	Wanted whole;
	whole.kind = Kind::I64;
	Slot size;
	size.bits = 0;
	if (!host_.handles.call(p_array, "size", nullptr, 0, whole, &size, &error)) {
		raise(module_.error_class, error);
		return nullptr;
	}
	Vector *made = fresh_vector(p_class, size.l < 0 ? 0 : static_cast<uint64_t>(size.l));
	Wanted untyped;
	untyped.kind = Kind::Any;
	for (uint32_t i = 0; made && i < made->length; i++) {
		Argument key;
		key.kind = Kind::I64;
		key.slot.bits = 0;
		key.slot.l = i;
		if (!host_.handles.index_get(p_array, key, untyped, &made->data[i], &error)) {
			release_object(made);
			raise(module_.error_class, error);
			return nullptr;
		}
	}
	return made;
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
	if (!as_text) {
		return false; // No memory for it: the error is on its way out.
	}
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

Slot Interpreter::present_or_null(Slot p_value) {
	return any::is_engine(p_value) && !host_.handles.present(any::pointer(p_value)) ? any::null() : p_value;
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
			// An object that speaks for itself is held while it does, by any_text().
			const Kind kind = p_function.registers[p_in.a];
			Slot out;
			if (from_any(b, kind, p_in.c, &out)) {
				result(kind, out);
			}
			break;
		}
		case Op::AnyText: {
			std::string words;
			if (any_text(b, &words)) {
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
			const bool function = any::tag(b) == AnyTag::Object && static_cast<const Object *>(any::pointer(b))->klass == module_.function_class;
			const std::string_view name = function ? std::string_view("function") : any::type_of(b);
			for (Str *each : type_names_) {
				out.p = each->view() == name ? each : out.p;
			}
			retain(static_cast<Str *>(out.p));
			result(Kind::Str, out);
			break;
		}
		case Op::AnyEq:
		case Op::AnyStrictEq: {
			// A Godot object that has been freed is null. Both are held only when one is a script object,
			// whose text may run code that lets go of the other.
			const Slot first = present_or_null(b);
			const Slot second = present_or_null(c);
			const bool speaks = any::tag(first) == AnyTag::Object || any::tag(second) == AnyTag::Object;
			const Slot x = speaks ? share(Kind::Any, first) : first;
			const Slot y = speaks ? share(Kind::Any, second) : second;
			bool equal = false;
			const bool done = any_equal(x, y, p_in.op == Op::AnyStrictEq, &equal);
			if (speaks) {
				drop_any(x);
				drop_any(y);
			}
			if (done) {
				result(Kind::Bool, of_bool(equal));
			}
			break;
		}
		case Op::AnyAdd:
		case Op::AnyLt:
		case Op::AnyLe:
		case Op::AnyGt:
		case Op::AnyGe:
			any_arithmetic(p_in, b, c);
			break;
		case Op::AnyIs:
			result(Kind::Bool, of_bool(any_is(b, p_in.c)));
			break;
		case Op::AnyIsGodot:
			result(Kind::Bool, of_bool(any::tag(b) == AnyTag::Godot && host_.handles.is_class(any::pointer(b), module_.strings[p_in.c])));
			break;
		default:
			break;
	}
}

// A number as an untyped value, held as AVM2 holds one: a whole number as an `int` where it fits one, and
// as an Int64 up to 2^53, where a Number is still exact; anything else (a fraction, -0, NaN, the very
// large) as a Number. So `v - 1` with `v` from Godot's integer 5 goes back to Godot as the integer 4.
// Undefined, with the error raised, when there is no memory for an Int64.
Slot Interpreter::any_number(double p_value) {
	constexpr double EXACT = 9007199254740992.0; // 2^53
	if (p_value != std::trunc(p_value) || std::fabs(p_value) > EXACT || (p_value == 0 && std::signbit(p_value))) {
		return any::number(p_value); // NaN fails the first test.
	}
	if (p_value >= INT32_MIN && p_value <= INT32_MAX) {
		return any::integer(static_cast<int32_t>(p_value));
	}
	return any_wide(static_cast<int64_t>(p_value));
}

Slot Interpreter::any_wide(int64_t p_value) {
	if (p_value >= INT32_MIN && p_value <= INT32_MAX) {
		return any::integer(static_cast<int32_t>(p_value));
	}
	Wide *wide = new_wide(p_value);
	if (!wide) {
		out_of_memory();
		return any::undefined();
	}
	return any::wide(wide);
}

// Two numbers added. Two whole ones stay whole; with an Int64 on a side, the sum wraps at 64 bits as a
// typed Int64's does.
Slot Interpreter::add_numbers(Slot p_a, Slot p_b) {
	const AnyTag a = any::tag(p_a);
	const AnyTag b = any::tag(p_b);
	const auto whole = [](Slot p_value, AnyTag p_tag) {
		return p_tag == AnyTag::Wide ? static_cast<const Wide *>(any::pointer(p_value))->value : static_cast<int64_t>(any::to_int_payload(p_value));
	};
	if ((a == AnyTag::Int || a == AnyTag::Wide) && (b == AnyTag::Int || b == AnyTag::Wide)) {
		const uint64_t sum = static_cast<uint64_t>(whole(p_a, a)) + static_cast<uint64_t>(whole(p_b, b));
		return any_wide(static_cast<int64_t>(sum));
	}
	return any_number(any::to_number(p_a) + any::to_number(p_b));
}

// `+` with a Godot value on a side and no text on either: Godot's own operator, where it has one
// (Vector2 + Vector2). True if that settled it, with the result or the error in place.
bool Interpreter::engine_add(const Instruction &p_in, Slot p_b, Slot p_c) {
	const AnyTag b = any::tag(p_b);
	const AnyTag c = any::tag(p_c);
	if ((!any::is_engine(p_b) && !any::is_engine(p_c)) || b == AnyTag::String || c == AnyTag::String ||
			b == AnyTag::Object || c == AnyTag::Object) {
		return false;
	}
	const Argument operands[2] = { Argument{ Kind::Any, p_b }, Argument{ Kind::Any, p_c } };
	Wanted wanted;
	wanted.kind = Kind::Any;
	Slot result;
	result.bits = 0;
	std::string error;
	if (!host_.handles.operate("+", operands, 2, wanted, &result, &error)) {
		if (error == OUT_OF_MEMORY) {
			raise(module_.error_class, error);
			return true;
		}
		return false; // No such operator in Godot: AS3's rule, which joins their text.
	}
	put(Kind::Any, stack_[frames_.back().base + p_in.a], result);
	return true;
}

// `+`, `<` and `<=` with an untyped operand, as ECMAScript has them: both become primitives; `+` joins
// text if either is text and adds numbers otherwise; `<` compares text if both are text.
void Interpreter::any_arithmetic(const Instruction &p_in, Slot p_b, Slot p_c) {
	const size_t base = frames_.back().base;
	if (p_in.op == Op::AnyAdd && engine_add(p_in, p_b, p_c)) {
		return;
	}
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
		// A string is joined as it is held; only the other side, a number or a word, is made into text.
		const auto view = [](Slot p_side, std::string *r_words) {
			if (any::tag(p_side) == AnyTag::String) {
				return static_cast<const Str *>(any::pointer(p_side))->view();
			}
			*r_words = *any::text(p_side);
			return std::string_view(*r_words);
		};
		std::string first_words, second_words;
		const std::string_view first_text = view(first, &first_words);
		Str *joined = join_strings(first_text, view(second, &second_words));
		if (!joined) {
			out_of_memory();
		} else {
			put(Kind::Any, stack_[base + p_in.a], any::string(joined));
		}
	} else if (p_in.op == Op::AnyAdd) {
		put(Kind::Any, stack_[base + p_in.a], add_numbers(first, second));
	} else {
		// `a > b` is `b < a` once both are primitives; the left one was made one first.
		const bool flipped = p_in.op == Op::AnyGt || p_in.op == Op::AnyGe;
		const bool or_equal = p_in.op == Op::AnyLe || p_in.op == Op::AnyGe;
		const Slot x_value = flipped ? second : first;
		const Slot y_value = flipped ? first : second;
		bool answer = false;
		if (any::tag(x_value) == AnyTag::String && any::tag(y_value) == AnyTag::String) {
			const std::string_view x = static_cast<Str *>(any::pointer(x_value))->view();
			const std::string_view y = static_cast<Str *>(any::pointer(y_value))->view();
			const int order = compare_utf16(x, y);
			answer = or_equal ? order <= 0 : order < 0;
		} else if (const std::optional<int> exact = any::compare_numbers(x_value, y_value)) {
			answer = or_equal ? *exact <= 0 : *exact < 0;
		}
		put(Kind::Bool, stack_[base + p_in.a], of_bool(answer));
	}
	drop_any(first);
	drop_any(second);
}

} // namespace vm
} // namespace kas3
