// The untyped value's operators: `==` where the values alone do not say, and `+`, `<` and the rest as
// ECMAScript has them.
#include "interpreter_internal.h"

#include <cmath>

namespace kas3 {
namespace vm {

using ir::Instruction;
using ir::Kind;
using ir::Op;

// `==` and `===` where the values alone do not say: two Godot values, which Godot compares, or an object
// against a text or a number, compared by its text.
bool Interpreter::any_equal(Slot p_a, Slot p_b, bool p_strict, bool *r_equal) {
	if (any::tag(p_a) == AnyTag::Object && any::tag(p_b) == AnyTag::Object &&
			same_function(static_cast<const Object *>(any::pointer(p_a)), static_cast<const Object *>(any::pointer(p_b)))) {
		*r_equal = true; // One method of one object, taken twice: as EqRef has it.
		return true;
	}
	// A script object whose class extends one of Godot's is that Godot object, as typed code has it
	// (`node == this`): against one of Godot's objects held as such, it is its Godot object that is compared.
	const auto as_godot = [this](Slot p_value) {
		Object *object = held_object(p_value);
		const ir::Class *klass = object ? &module_.classes[object->klass] : nullptr;
		const bool owned = klass && !klass->native_base.empty() && !klass->fields.empty() && object->fields()[0].p;
		return owned ? any::godot_object(object->fields()[0].p) : p_value;
	};
	if ((any::tag(p_a) == AnyTag::Godot && any::tag(p_b) == AnyTag::Object) || (any::tag(p_a) == AnyTag::Object && any::tag(p_b) == AnyTag::Godot)) {
		p_a = as_godot(p_a);
		p_b = as_godot(p_b);
	}
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
		const OperatorName equal = operator_named(module_.equal_operator, "==");
		if (!host_.handles.operate(equal.number, equal.spelling, operands, 2, wanted, &result, &error)) {
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
	if (any::tag(p_b) == AnyTag::String || any::tag(p_c) == AnyTag::String) {
		return false;
	}
	const Slot operands[2] = { p_b, p_c };
	return engine_operate(operator_named(module_.add_operator, "+"), operands, 2, Kind::Any, p_in.a);
}

// An operator on one or two untyped values, one of them a Godot value and none an object of the program's:
// Godot's own, where it has one for what they hold, as typed code has it (Vector2 * 3). True if that
// settled it, with the result or the error in place; false leaves it to AS3's rule.
bool Interpreter::engine_operate(const OperatorName &p_operator, const Slot *p_operands, size_t p_count, Kind p_result, uint32_t p_out) {
	bool engine = false;
	Argument operands[2];
	for (size_t i = 0; i < p_count; i++) {
		engine = engine || any::is_engine(p_operands[i]);
		if (any::tag(p_operands[i]) == AnyTag::Object) {
			return false;
		}
		operands[i] = Argument{ Kind::Any, p_operands[i] };
	}
	if (!engine) {
		return false;
	}
	Wanted wanted;
	wanted.kind = p_result;
	Slot result;
	result.bits = 0;
	std::string error;
	if (!host_.handles.operate(p_operator.number, p_operator.spelling, operands, p_count, wanted, &result, &error)) {
		if (error == OUT_OF_MEMORY) {
			raise(module_.error_class, error);
			return true;
		}
		return false; // No such operator in Godot for these.
	}
	put(p_result, stack_[frames_.back().base + p_out], result);
	return true;
}

// AnySub and the rest: Godot's operator where an operand holds one of Godot's values; otherwise, and where
// Godot has none for them, AS3's, which makes both Numbers.
void Interpreter::any_operate(const Instruction &p_in, Slot p_b, Slot p_c) {
	const bool two = p_in.op != Op::AnyNeg;
	// Each stays held by its register, which nothing changes while this runs.
	const Slot operands[2] = { p_b, two ? p_c : any::undefined() };
	if (any::is_engine(operands[0]) || any::is_engine(operands[1])) {
		const OperatorName named = p_in.op == Op::AnySub ? operator_named(module_.subtract_operator, "-")
				: p_in.op == Op::AnyMul ? operator_named(module_.multiply_operator, "*")
				: p_in.op == Op::AnyDiv ? operator_named(module_.divide_operator, "/")
				: p_in.op == Op::AnyMod ? operator_named(module_.modulo_operator, "%") : operator_named(module_.negate_operator, "-");
		if (engine_operate(named, operands, two ? 2 : 1, Kind::Any, p_in.a)) {
			return;
		}
	}
	Slot numbers[2];
	numbers[0].bits = numbers[1].bits = 0;
	for (size_t i = 0; i < (two ? 2 : 1); i++) {
		if (!from_any(operands[i], Kind::F64, ir::NONE, &numbers[i])) {
			return;
		}
	}
	Slot out;
	out.bits = 0;
	switch (p_in.op) {
		case Op::AnyMul:
			out.d = numbers[0].d * numbers[1].d;
			break;
		case Op::AnyDiv:
			out.d = numbers[0].d / numbers[1].d;
			break;
		case Op::AnyMod:
			out.d = std::fmod(numbers[0].d, numbers[1].d);
			break;
		case Op::AnySub:
			out.d = numbers[0].d - numbers[1].d;
			break;
		default:
			out.d = -numbers[0].d;
			break;
	}
	put(Kind::Any, stack_[frames_.back().base + p_in.a], to_any(Kind::F64, out));
}

// `+`, `<` and `<=` with an untyped operand, as ECMAScript has them: both become primitives; `+` joins
// text if either is text and adds numbers otherwise; `<` compares text if both are text.
void Interpreter::any_arithmetic(const Instruction &p_in, Slot p_b, Slot p_c) {
	const size_t base = frames_.back().base;
	if (p_in.op == Op::AnyAdd && engine_add(p_in, p_b, p_c)) {
		return;
	}
	if (p_in.op != Op::AnyAdd && (any::is_engine(p_b) || any::is_engine(p_c))) {
		// Godot's own order for its values (Vector2 < Vector2), as typed code has it.
		const OperatorName named = p_in.op == Op::AnyLt ? operator_named(module_.less_operator, "<")
				: p_in.op == Op::AnyLe ? operator_named(module_.less_equal_operator, "<=")
				: p_in.op == Op::AnyGt ? operator_named(module_.greater_operator, ">") : operator_named(module_.greater_equal_operator, ">=");
		const Slot operands[2] = { p_b, p_c };
		if (engine_operate(named, operands, 2, Kind::Bool, p_in.a)) {
			return;
		}
	}
	Slot first;
	Slot second;
	const bool number = p_in.op != Op::AnyAdd; // `+` takes a Date's text; `<` its time.
	if (!any_primitive(p_b, &first, number)) {
		return;
	}
	if (!any_primitive(p_c, &second, number)) {
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
