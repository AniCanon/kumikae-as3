// The untyped value's operators as functions of plain values (vm/library.h): `==` and `===` where the
// values alone do not say, `+`, `<`, `-` and the rest as ECMAScript has them, or as Godot has them for its
// own values, and whether a vector's element is the value looked for.
#include "library.h"

#include <cmath>

namespace kas3 {
namespace vm {

using ir::Kind;
using ir::Op;

namespace {

Slot of_bool(bool p_value) {
	Slot out;
	out.bits = 0;
	out.b = p_value;
	return out;
}

// Whether two elements are the same value, as `===` has it.
bool same_element(Kind p_kind, Slot p_a, Slot p_b) {
	switch (p_kind) {
		case Kind::F32:
		case Kind::F64:
			return p_a.d == p_b.d;
		case Kind::Bool:
			return p_a.b == p_b.b;
		case Kind::I64:
			return p_a.l == p_b.l;
		case Kind::Str: {
			const Str *a = static_cast<const Str *>(p_a.p);
			const Str *b = static_cast<const Str *>(p_b.p);
			return a && b ? a->view() == b->view() : a == b;
		}
		case Kind::Obj:
			return p_a.p == p_b.p;
		default:
			return p_a.u == p_b.u;
	}
}

// What Godot's `==` says of two of its values or objects, each given as the kind it is held as. False
// where Godot has no `==` between them.
bool engine_equal(Library &p_library, Kind p_first, void *p_a, Kind p_second, void *p_b) {
	Argument operands[2] = { Argument{ p_first, Slot{} }, Argument{ p_second, Slot{} } };
	operands[0].slot.p = p_a;
	operands[1].slot.p = p_b;
	Wanted wanted;
	wanted.kind = Kind::Bool;
	Slot result;
	result.bits = 0;
	std::string error;
	const OperatorName equal = p_library.heap.operator_named(p_library.module().equal_operator, "==");
	return p_library.heap.handles.operate(equal.number, equal.spelling, operands, 2, wanted, &result, &error) && result.b;
}

// `==` and `===` where the values alone do not say: two Godot values, which Godot compares, or an object
// against a text or a number, compared by its text.
bool any_equal(Library &p_library, Slot p_a, Slot p_b, bool p_strict, bool *r_equal) {
	const ir::Module &module = p_library.module();
	if (any::tag(p_a) == AnyTag::Object && any::tag(p_b) == AnyTag::Object &&
			same_function(p_library, static_cast<const Object *>(any::pointer(p_a)), static_cast<const Object *>(any::pointer(p_b)))) {
		*r_equal = true; // One method of one object, taken twice: as EqRef has it.
		return true;
	}
	// A script object whose class extends one of Godot's is that Godot object, as typed code has it
	// (`node == this`): against one of Godot's objects held as such, it is its Godot object that is compared.
	const auto as_godot = [&module](Slot p_value) {
		Object *object = held_object(p_value);
		const ir::Class *klass = object ? &module.classes[object->klass] : nullptr;
		const bool owned = klass && !klass->native_base.empty() && !klass->fields.empty() && owner_field(object).p;
		return owned ? any::godot_object(owner_field(object).p) : p_value;
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
		// Where Godot has no `==` between these two, they are not equal.
		*r_equal = engine_equal(p_library, any::tag(p_a) == AnyTag::Value ? Kind::Val : Kind::Ref, any::pointer(p_a),
				any::tag(p_b) == AnyTag::Value ? Kind::Val : Kind::Ref, any::pointer(p_b));
		return true;
	}
	// An object and a primitive, by `==`: the object's text against the other.
	const bool first_is_object = any::tag(p_a) == AnyTag::Object || any::is_engine(p_a);
	std::string words;
	if (!any_text(p_library, first_is_object ? p_a : p_b, &words)) {
		return false;
	}
	Str *as_text = p_library.text(words);
	if (!as_text) {
		return false; // No memory for it: the error is on its way out.
	}
	*r_equal = any::loose_equal(any::string(as_text), first_is_object ? p_b : p_a).value_or(false);
	free_string(as_text);
	return true;
}

// Two numbers added. Two whole ones stay whole; with an Int64 on a side, the sum wraps at 64 bits as a
// typed Int64's does.
Slot add_numbers(Library &p_library, Slot p_a, Slot p_b) {
	const AnyTag a = any::tag(p_a);
	const AnyTag b = any::tag(p_b);
	const auto whole = [](Slot p_value, AnyTag p_tag) {
		return p_tag == AnyTag::Wide ? static_cast<const Wide *>(any::pointer(p_value))->value : static_cast<int64_t>(any::to_int_payload(p_value));
	};
	if ((a == AnyTag::Int || a == AnyTag::Wide) && (b == AnyTag::Int || b == AnyTag::Wide)) {
		const uint64_t sum = static_cast<uint64_t>(whole(p_a, a)) + static_cast<uint64_t>(whole(p_b, b));
		return any_wide(p_library, static_cast<int64_t>(sum));
	}
	return any_number(p_library, any::to_number(p_a) + any::to_number(p_b));
}

// Godot's name for the operator an instruction is, as the module has it.
OperatorName operator_of(const Library &p_library, Op p_op) {
	const ir::Module &module = p_library.module();
	const Heap &heap = p_library.heap;
	switch (p_op) {
		case Op::AnyAdd:
			return heap.operator_named(module.add_operator, "+");
		case Op::AnyLt:
			return heap.operator_named(module.less_operator, "<");
		case Op::AnyLe:
			return heap.operator_named(module.less_equal_operator, "<=");
		case Op::AnyGt:
			return heap.operator_named(module.greater_operator, ">");
		case Op::AnyGe:
			return heap.operator_named(module.greater_equal_operator, ">=");
		case Op::AnySub:
			return heap.operator_named(module.subtract_operator, "-");
		case Op::AnyMul:
			return heap.operator_named(module.multiply_operator, "*");
		case Op::AnyDiv:
			return heap.operator_named(module.divide_operator, "/");
		case Op::AnyMod:
			return heap.operator_named(module.modulo_operator, "%");
		default:
			return heap.operator_named(module.negate_operator, "-");
	}
}

// An operator on one or two untyped values, one of them a Godot value and none an object of the program's:
// Godot's own, where it has one for what they hold, as typed code has it (Vector2 * 3). True if that
// settled it, with the result (of the kind asked for) in *r_out or the error raised; false leaves it to
// AS3's rule.
bool engine_operate(Library &p_library, Op p_op, const Slot *p_operands, size_t p_count, Kind p_result, Slot *r_out) {
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
	const OperatorName named = operator_of(p_library, p_op);
	if (!p_library.heap.handles.operate(named.number, named.spelling, operands, p_count, wanted, &result, &error)) {
		if (error == OUT_OF_MEMORY) {
			p_library.runner.raise(p_library.module().error_class, error);
			return true;
		}
		return false; // No such operator in Godot for these.
	}
	*r_out = result;
	return true;
}

// AnySub and the rest: Godot's operator where an operand holds one of Godot's values; otherwise, and where
// Godot has none for them, AS3's, which makes both Numbers.
bool any_numbers(Library &p_library, Op p_op, const Slot *p_values, size_t p_count, Slot *r_out) {
	if (engine_operate(p_library, p_op, p_values, p_count, Kind::Any, r_out)) {
		return !p_library.runner.failing();
	}
	Slot numbers[2];
	numbers[0].bits = numbers[1].bits = 0;
	for (size_t i = 0; i < p_count; i++) {
		if (!from_any(p_library, p_values[i], Kind::F64, ir::NONE, &numbers[i])) {
			return false;
		}
	}
	Slot out;
	out.bits = 0;
	switch (p_op) {
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
	*r_out = to_any(p_library, Kind::F64, out);
	return !p_library.runner.failing();
}

// Two primitives joined as text, where either is text: a string is joined as it is held; only the other
// side, a number or a word, is made into text. False, with Error #1000 raised, for no memory.
bool join_primitives(Library &p_library, Slot p_first, Slot p_second, Slot *r_out) {
	const auto view = [](Slot p_side, std::string *r_words) {
		if (any::tag(p_side) == AnyTag::String) {
			return static_cast<const Str *>(any::pointer(p_side))->view();
		}
		*r_words = *any::text(p_side);
		return std::string_view(*r_words);
	};
	std::string first_words, second_words;
	const std::string_view first_text = view(p_first, &first_words);
	Str *joined = join_strings(first_text, view(p_second, &second_words));
	if (!joined) {
		p_library.runner.out_of_memory();
		return false;
	}
	*r_out = any::string(joined);
	return true;
}

// `<` and the rest of two primitives: by text if both are text, and as numbers otherwise.
bool ordered(Op p_op, Slot p_first, Slot p_second) {
	// `a > b` is `b < a` once both are primitives; the left one was made one first.
	const bool flipped = p_op == Op::AnyGt || p_op == Op::AnyGe;
	const bool or_equal = p_op == Op::AnyLe || p_op == Op::AnyGe;
	const Slot x_value = flipped ? p_second : p_first;
	const Slot y_value = flipped ? p_first : p_second;
	if (any::tag(x_value) == AnyTag::String && any::tag(y_value) == AnyTag::String) {
		const int order = compare_utf16(static_cast<Str *>(any::pointer(x_value))->view(), static_cast<Str *>(any::pointer(y_value))->view());
		return or_equal ? order <= 0 : order < 0;
	}
	if (const std::optional<int> exact = any::compare_numbers(x_value, y_value)) {
		return or_equal ? *exact <= 0 : *exact < 0;
	}
	return false;
}

// `+`, `<` and `<=` with an untyped operand, as ECMAScript has them: both become primitives; `+` joins
// text if either is text and adds numbers otherwise.
bool any_arithmetic(Library &p_library, Op p_op, Slot p_b, Slot p_c, Slot *r_out) {
	// `+` with a Godot value on a side and no text on either is Godot's own operator, where it has one
	// (Vector2 + Vector2); so is Godot's own order for its values (Vector2 < Vector2), as typed code has it.
	const bool texts = p_op == Op::AnyAdd && (any::tag(p_b) == AnyTag::String || any::tag(p_c) == AnyTag::String);
	const Slot operands[2] = { p_b, p_c };
	if (!texts && engine_operate(p_library, p_op, operands, 2, p_op == Op::AnyAdd ? Kind::Any : Kind::Bool, r_out)) {
		return !p_library.runner.failing();
	}
	Slot first;
	Slot second;
	const bool number = p_op != Op::AnyAdd; // `+` takes a Date's text; `<` its time.
	if (!any_primitive(p_library, p_b, &first, number)) {
		return false;
	}
	if (!any_primitive(p_library, p_c, &second, number)) {
		p_library.heap.drop_any(first);
		return false;
	}
	bool done = true;
	if (p_op != Op::AnyAdd) {
		*r_out = of_bool(ordered(p_op, first, second));
	} else if (any::tag(first) == AnyTag::String || any::tag(second) == AnyTag::String) {
		done = join_primitives(p_library, first, second, r_out);
	} else {
		*r_out = add_numbers(p_library, first, second);
		done = !p_library.runner.failing();
	}
	p_library.heap.drop_any(first);
	p_library.heap.drop_any(second);
	return done;
}

} // namespace

const Vector *bound_pair(const ir::Module &p_module, const Object *p_function) {
	if (!p_function || p_function->klass != p_module.function_class) {
		return nullptr;
	}
	Object *function = const_cast<Object *>(p_function);
	const auto index = static_cast<uint32_t>(own_field(function, ir::FUNCTION_INDEX).i);
	if (index >= p_module.functions.size() || (index != p_module.library_function && !p_module.functions[index].bound_engine_method)) {
		return nullptr;
	}
	const Object *first = static_cast<const Object *>(own_field(function, ir::FUNCTION_FIRST).p);
	if (!first || first->klass != p_module.array_class) {
		return nullptr;
	}
	const auto *bound = static_cast<const Vector *>(first);
	return bound->length >= 2 && any::tag(slots_of(bound)[1]) == AnyTag::String ? bound : nullptr;
}

bool engine_method(const ir::Module &p_module, const Object *p_function, void **r_object, std::string_view *r_method) {
	const Vector *bound = bound_pair(p_module, p_function);
	if (!bound || bound->length < 3 || any::tag(slots_of(bound)[0]) != AnyTag::Godot || any::tag(slots_of(bound)[2]) != AnyTag::String) {
		return false;
	}
	*r_object = any::pointer(slots_of(bound)[0]);
	*r_method = static_cast<const Str *>(any::pointer(slots_of(bound)[2]))->view();
	return true;
}

bool same_function(Library &p_library, const Object *p_a, const Object *p_b) {
	const ir::Module &module = p_library.module();
	if (!p_a || !p_b || p_a->klass != module.function_class || p_b->klass != module.function_class) {
		return false;
	}
	Object *a = const_cast<Object *>(p_a);
	Object *b = const_cast<Object *>(p_b);
	const Vector *bound_a = bound_pair(module, a);
	const Vector *bound_b = bound_pair(module, b);
	if (bound_a && bound_b) {
		// A method of the library or one of Godot's, which both are (what such a one is bound to is a pair,
		// the value and the name, where any other function's is its object): the same when read from the
		// same value by the same name. One of Godot's values or objects is the same where Godot says so.
		const Slot held_a = slots_of(bound_a)[0], held_b = slots_of(bound_b)[0];
		const bool named = static_cast<const Str *>(any::pointer(slots_of(bound_a)[1]))->view() == static_cast<const Str *>(any::pointer(slots_of(bound_b)[1]))->view();
		if (!named || any::is_engine(held_a) != any::is_engine(held_b)) {
			return false;
		}
		if (any::is_engine(held_a)) {
			return engine_equal(p_library, any::tag(held_a) == AnyTag::Value ? Kind::Val : Kind::Ref, any::pointer(held_a),
					any::tag(held_b) == AnyTag::Value ? Kind::Val : Kind::Ref, any::pointer(held_b));
		}
		return held_a.bits == held_b.bits && own_field(a, ir::FUNCTION_INDEX).i == own_field(b, ir::FUNCTION_INDEX).i;
	}
	if (bound_a || bound_b || own_field(a, ir::FUNCTION_INDEX).i != own_field(b, ir::FUNCTION_INDEX).i) {
		return false;
	}
	return own_field(a, ir::FUNCTION_FIRST).p == own_field(b, ir::FUNCTION_FIRST).p && own_field(a, ir::FUNCTION_FIRST).p;
}

// A Godot object that has been freed is null. Both are held only when one is a script object, whose text
// may run code that lets go of the other.
bool any_equality(Library &p_library, Slot p_a, Slot p_b, bool p_strict, bool *r_equal) {
	const Slot first = present_or_null(p_library, p_a);
	const Slot second = present_or_null(p_library, p_b);
	const bool speaks = any::tag(first) == AnyTag::Object || any::tag(second) == AnyTag::Object;
	const Slot x = speaks ? p_library.heap.share(Kind::Any, first) : first;
	const Slot y = speaks ? p_library.heap.share(Kind::Any, second) : second;
	const bool done = any_equal(p_library, x, y, p_strict, r_equal);
	if (speaks) {
		p_library.heap.drop_any(x);
		p_library.heap.drop_any(y);
	}
	return done;
}

bool any_operate(Library &p_library, Op p_op, const Slot *p_values, size_t p_count, Slot *r_out) {
	if (p_op == Op::AnyEq || p_op == Op::AnyStrictEq) {
		bool equal = false;
		if (!any_equality(p_library, p_values[0], p_values[1], p_op == Op::AnyStrictEq, &equal)) {
			return false;
		}
		*r_out = of_bool(equal);
		return true;
	}
	if (p_op == Op::AnySub || p_op == Op::AnyMul || p_op == Op::AnyDiv || p_op == Op::AnyMod || p_op == Op::AnyNeg) {
		return any_numbers(p_library, p_op, p_values, p_count, r_out);
	}
	return any_arithmetic(p_library, p_op, p_values[0], p_values[1], r_out);
}

// One of Godot's values or objects is the same as Godot says: the same object, or an equal value. Null is
// only null.
bool element_is(Library &p_library, Kind p_kind, Slot p_element, Slot p_value) {
	if (p_kind == Kind::Any) {
		bool equal = false;
		return any_equal(p_library, p_element, p_value, true, &equal) && equal;
	}
	if (!ir::is_engine(p_kind)) {
		return same_element(p_kind, p_element, p_value);
	}
	if (!p_element.p || !p_value.p) {
		return p_element.p == p_value.p;
	}
	return engine_equal(p_library, p_kind, p_element.p, p_kind, p_value.p);
}

} // namespace vm
} // namespace kas3
