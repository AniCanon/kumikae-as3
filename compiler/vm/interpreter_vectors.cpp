#include "interpreter_internal.h"

#include "text_units.h"
#include "vector_calls.h"

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

// Whether two elements are the same value, as `===` has it.
bool same_element(Kind p_kind, Slot p_a, Slot p_b) {
	switch (p_kind) {
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

} // namespace

// Whether an element is the value looked for. One of Godot's values or objects is the same as Godot says:
// the same object, or an equal value. Null is only null.
bool Interpreter::element_is(Kind p_kind, Slot p_element, Slot p_value) {
	if (p_kind == Kind::Any) {
		bool equal = false;
		return any_equal(p_element, p_value, true, &equal) && equal;
	}
	if (!ir::is_engine(p_kind)) {
		return same_element(p_kind, p_element, p_value);
	}
	if (!p_element.p || !p_value.p) {
		return p_element.p == p_value.p;
	}
	const Argument both[2] = { Argument{ p_kind, p_element }, Argument{ p_kind, p_value } };
	Wanted wanted;
	wanted.kind = Kind::Bool;
	Slot equal;
	equal.bits = 0;
	std::string error;
	const OperatorName same = operator_named(module_.equal_operator, "==");
	return host_.handles.operate(same.number, same.spelling, both, 2, wanted, &equal, &error) && equal.b;
}

// The element's property of that name, or undefined when it has none (a value that is not an object, or
// an object without it): sortOn never raises #1069.
bool Interpreter::sort_field(Slot p_element, Str *p_name, Slot *r_value) {
	*r_value = any::undefined();
	if (!held_object(p_element)) {
		return true;
	}
	PropertyKey key;
	bool has = false;
	if (!property_key(p_element, any::string(p_name), &key) || !has_property(p_element, key, false, &has)) {
		return false;
	}
	return !has || get_property(p_element, key, r_value);
}

// VecCall: the method is the library's function of plain values, handed the values themselves (the
// registers may move as it runs the program); what it gives is stored where the register then is.
void Interpreter::vector_call(const Function &p_function, const Instruction &p_in) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const size_t base = frames_.back().base;
	SpareSlots given(spare_slots_);
	for (const uint32_t each : list) {
		given->push_back(stack_[base + each]);
	}
	Slot made;
	made.bits = 0;
	const bool done = vm::vector_call(library_, static_cast<ir::VectorMethod>(p_in.b), given->data(), given->size(), p_in.a == NONE ? nullptr : &made);
	if (done && p_in.a != NONE) {
		put(p_function.registers[p_in.a], stack_[frames_.back().base + p_in.a], made);
	}
}

// The instructions on a vector's elements and length. An element in range is read and written here; the
// rest (an index outside, a vector that grows, a new one) is the library's function.
void Interpreter::vector_op(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	Slot *r = p_registers;
	if (p_in.op == Op::ArrayNew || p_in.op == Op::VecNew) {
		const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
		Vector *made = nullptr;
		if (p_in.op == Op::VecNew) {
			made = vm::vector_new(library_, p_in.b, r[list[0]].u, r[list[1]].b);
		} else {
			SpareSlots given(spare_slots_);
			for (const uint32_t each : list) {
				given->push_back(r[each]);
			}
			made = vm::array_new(library_, p_in.b, given->data(), given->size());
		}
		if (made) {
			assign(Kind::Obj, r[p_in.a], made);
		}
		return;
	}
	const bool reads = p_in.op == Op::VecGet || p_in.op == Op::VecLength || p_in.op == Op::VecFixed;
	Vector *vector = static_cast<Vector *>(r[reads ? p_in.b : p_in.a].p);
	if (!vector) {
		raise_null();
		return;
	}
	const Kind kind = module_.classes[vector->klass].element;
	switch (p_in.op) {
		case Op::VecGet: {
			const Kind index = p_function.registers[p_in.c];
			if (const std::optional<uint32_t> at = element_index(index, r[p_in.c], vector->length)) {
				put(kind, r[p_in.a], share(kind, vector->data[*at]));
				break;
			}
			Slot made;
			made.bits = 0;
			if (vm::vector_get(library_, vector, index, r[p_in.c], &made)) {
				put(kind, r[p_in.a], made);
			}
			break;
		}
		case Op::VecSet: {
			const Kind index = p_function.registers[p_in.b];
			if (const std::optional<uint32_t> at = element_index(index, r[p_in.b], vector->length)) {
				put(kind, vector->data[*at], share(kind, r[p_in.c]));
			} else {
				vm::vector_set(library_, vector, index, r[p_in.b], r[p_in.c]);
			}
			break;
		}
		case Op::VecLength:
			set_uint32(r[p_in.a], vector->length);
			break;
		case Op::VecSetLength:
			vm::vector_set_length(library_, vector, r[p_in.b].u);
			break;
		case Op::VecFixed:
			r[p_in.a] = of_bool(vector->fixed);
			break;
		case Op::VecSetFixed:
			vector->fixed = r[p_in.b].b;
			break;
		default:
			break;
	}
}

// VecFrom, Vector.<T>(x), by the library's function: converting an element may run the program.
void Interpreter::vector_from(const Instruction &p_in) {
	const Slot source = share(Kind::Any, stack_[frames_.back().base + p_in.b]);
	Slot made;
	made.bits = 0;
	if (vm::vector_from(library_, p_in.c, source, &made)) {
		put(Kind::Obj, stack_[frames_.back().base + p_in.a], made);
	}
	drop_any(source);
}

// The elements as text with p_separator between them, as join() and toString() give it, or with p_locale
// as toLocaleString() does. An element that is an object says what it is in its own words, which runs the
// program: false if that failed.
bool Interpreter::vector_text(Vector *p_vector, std::string_view p_separator, std::string *r_text, bool p_locale) {
	if (text_depth_ >= MAX_TEXT_DEPTH) {
		stack_overflow(); // One that holds itself ends here too: avmplus's join has no guard but the stack's.
		return false;
	}
	text_depth_++;
	const Kind kind = module_.classes[p_vector->klass].element;
	// An Array leaves null and undefined out (avmplus's Array.as, _join: `if (x != null)`, as ECMAScript 3
	// has it), and so does toLocaleString() of either (VectorImpl.as: `if (x !== undefined && x !== null)`);
	// a Vector's join writes them as String() does (VectorImpl.as, join: `s += this[y]`).
	const bool leaves_out = p_locale || (module_.classes[p_vector->klass].array && kind == Kind::Any);
	bool done = true;
	for (uint32_t i = 0; done && i < p_vector->length; i++) {
		if (i) {
			append_text(r_text, p_separator); // Text is joined as Concat joins it: two halves that meet are one character.
		}
		const Slot element = p_vector->data[i];
		// A freed Godot object is null here too.
		const AnyTag tag = kind == Kind::Any ? any::tag(present_or_null(element)) : AnyTag::Undefined;
		const bool engine = kind == Kind::Ref || kind == Kind::Val;
		const bool nothing = kind == Kind::Any ? tag == AnyTag::Null || tag == AnyTag::Undefined
											   : held(kind) && (!element.p || (engine && !host_.handles.present(element.p)));
		if (leaves_out && nothing) {
			continue;
		}
		done = p_locale ? locale_element_text(kind, element, r_text) : element_text(kind, element, r_text);
	}
	text_depth_--;
	return done;
}

// What the object's method gives when called with no arguments, as text, added to r_text. False if the
// method failed, or needs an argument (AS3's ArgumentError #1063).
bool Interpreter::said_by(uint32_t p_method, Object *p_object, std::string *r_text) {
	std::vector<Slot> self;
	if (!function_arguments(p_method, p_object, 0, nullptr, &self)) {
		return false;
	}
	Slot result;
	result.bits = 0;
	retain(p_object); // Held while it speaks, for the reason element_text() gives.
	const bool called = call_within(p_method, self.data(), self.size(), &result);
	release_object(p_object);
	if (!called) {
		return false;
	}
	const Kind kind = module_.functions[p_method].result;
	const Slot given = kind != Kind::Void ? to_any(kind, result) : any::undefined();
	drop(kind, result);
	std::string said;
	const bool done = any_text(given, &said);
	drop_any(given);
	if (done) {
		append_text(r_text, said);
	}
	return done;
}

// An element as toLocaleString() asks for it: a vector, or a Date, in its own locale words; anything else
// as its text.
bool Interpreter::locale_element_text(Kind p_kind, Slot p_element, std::string *r_text) {
	Object *object = p_kind == Kind::Obj ? static_cast<Object *>(p_element.p) : p_kind == Kind::Any ? held_object(p_element) : nullptr;
	if (object && object->klass == module_.date_class) {
		*r_text += date_text(object, ir::DateText::Locale);
		return true;
	}
	if (!object || module_.classes[object->klass].element == Kind::Void) {
		// A class's own toLocaleString() is what avmplus asks each element for; Object's is its toString().
		const uint32_t own = object ? find_method(object->klass, "toLocaleString") : NONE;
		const bool callable = own != NONE && module_.functions[own].is_public && !module_.functions[own].is_static;
		return callable ? said_by(own, object, r_text) : element_text(p_kind, p_element, r_text);
	}
	retain(object); // Held while its elements speak, for the reason element_text() gives.
	const bool said = vector_text(static_cast<Vector *>(object), ",", r_text, true);
	release_object(object);
	return said;
}

// One element of a vector of that kind as text, added to r_text. False if what it said failed.
bool Interpreter::element_text(Kind p_kind, Slot p_element, std::string *r_text) {
	switch (p_kind) {
		case Kind::I32:
			*r_text += std::to_string(p_element.i);
			return true;
		case Kind::U32:
			*r_text += std::to_string(p_element.u);
			return true;
		case Kind::I64:
			*r_text += std::to_string(p_element.l);
			return true;
		case Kind::F64:
			*r_text += number_to_string(p_element.d);
			return true;
		case Kind::Bool:
			*r_text += p_element.b ? "true" : "false";
			return true;
		case Kind::Str:
			append_text(r_text, p_element.p ? static_cast<Str *>(p_element.p)->view() : std::string_view("null"));
			return true;
		case Kind::Any: {
			// Held while it speaks, for the reason below.
			const Slot held = share(Kind::Any, p_element);
			std::string said;
			const bool done = any_text(held, &said);
			drop_any(held);
			if (!done) {
				return false;
			}
			append_text(r_text, said);
			return true;
		}
		case Kind::Obj: {
			// Held while it speaks: what it says may run the program, which may take it out of
			// this vector, and then nothing else would be holding it.
			Object *object = static_cast<Object *>(p_element.p);
			retain(object);
			const bool said = object_as_text(object, r_text);
			release_object(object);
			return said;
		}
		default:
			// A handle of its own, for the same reason: Godot's text for an object can run a script.
			return engine_text(p_element.p, r_text);
	}
}

// An object as String() gives it: a vector's elements, a class's own toString(), or "[object Name]".
bool Interpreter::object_as_text(Object *p_object, std::string *r_text) {
	if (!p_object) {
		*r_text += "null";
		return true;
	}
	const ir::Class &klass = module_.classes[p_object->klass];
	if (klass.element != Kind::Void) {
		return vector_text(static_cast<Vector *>(p_object), ",", r_text);
	}
	bool own = false;
	if (klass.table && (!table_text(p_object, r_text, &own) || own)) {
		return !failing_;
	}
	if (klass.to_string == NONE) {
		*r_text += object_text(p_object);
		return true;
	}
	Slot self;
	self.bits = 0;
	self.p = p_object;
	Slot result;
	result.bits = 0;
	if (!call_within(klass.to_string, &self, 1, &result)) {
		return false; // Its error is on its way out of the code that asked.
	}
	Str *text = static_cast<Str *>(result.p);
	*r_text += text ? text->view() : std::string_view("null");
	if (release(text)) {
		free_string(text);
	}
	return true;
}

} // namespace vm
} // namespace kas3
