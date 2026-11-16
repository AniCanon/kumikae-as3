#include "interpreter_internal.h"

#include "text_units.h"

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

// The element an index names, if it is a whole number from 0 to p_limit - 1.
std::optional<uint32_t> element_index(double p_index, uint32_t p_limit) {
	if (!(p_index >= 0 && p_index < static_cast<double>(p_limit))) {
		return std::nullopt;
	}
	const uint32_t whole = static_cast<uint32_t>(p_index);
	return static_cast<double>(whole) == p_index ? std::optional<uint32_t>(whole) : std::nullopt;
}

// An index of any kind a register may declare for it, as a Number: what an error about it says.
double index_number(Kind p_kind, const Slot &p_index) {
	return p_kind == Kind::I32 ? p_index.i : p_kind == Kind::U32 ? (double)p_index.u : p_index.d;
}

// The same for an index held as an int or a uint, read as it is.
std::optional<uint32_t> element_index(Kind p_kind, const Slot &p_index, uint32_t p_limit) {
	if (p_kind == Kind::I32) {
		return p_index.i >= 0 && (uint32_t)p_index.i < p_limit ? std::optional<uint32_t>((uint32_t)p_index.i) : std::nullopt;
	}
	if (p_kind == Kind::U32) {
		return p_index.u < p_limit ? std::optional<uint32_t>(p_index.u) : std::nullopt;
	}
	return element_index(p_index.d, p_limit);
}

} // namespace

void Interpreter::raise_range(double p_index, uint32_t p_length) {
	raise(module_.range_error_class,
			"Error #1125: The index " + number_to_string(p_index) + " is out of range " + std::to_string(p_length) + ".");
}

// new Array(), Array(n), new Array(a, b): one argument that is a number is the length, which must be a whole
// number below 2^32 (AS3's RangeError #1005); anything else is the elements.
void Interpreter::array_new(const Function &p_function, const Instruction &p_in) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const size_t base = frames_.back().base;
	const auto given = [&](size_t p_index) { return stack_[base + list[p_index]]; };
	const AnyTag tag = list.size() == 1 ? any::tag(given(0)) : AnyTag::Undefined;
	uint64_t length = list.size();
	bool spread = true;
	if (tag == AnyTag::Int || tag == AnyTag::Number || tag == AnyTag::Wide) {
		const double asked = tag == AnyTag::Wide ? static_cast<double>(static_cast<Wide *>(any::pointer(given(0)))->value) : any::to_number(given(0));
		if (!(asked >= 0 && asked <= 4294967295.0 && asked == static_cast<double>(static_cast<uint64_t>(asked)))) {
			raise(module_.range_error_class, "Error #1005: Array index is not a positive integer (" + number_to_string(asked) + ").");
			return;
		}
		length = static_cast<uint64_t>(asked);
		spread = false;
	}
	Vector *made = fresh_vector(p_in.b, length);
	if (!made) {
		return;
	}
	for (size_t i = 0; spread && i < list.size(); i++) {
		made->data[i] = share(Kind::Any, given(i));
	}
	Slot out;
	out.bits = 0;
	out.p = made;
	put(Kind::Obj, stack_[base + p_in.a], out);
}

// Gives the vector the length asked for: elements past it are let go, and new ones are empty.
bool Interpreter::resize_vector(Vector *p_vector, uint32_t p_length) {
	if (length_is_fixed(p_vector)) {
		return false;
	}
	if (p_length > MAX_VECTOR_LENGTH || !vector_reserve(p_vector, p_length)) {
		out_of_memory();
		return false;
	}
	const Kind kind = module_.classes[p_vector->klass].element;
	const uint32_t old = p_vector->length;
	if (p_length >= old) {
		p_vector->length = p_length;
		return true;
	}
	// Every element that goes is taken out before any is let go: letting one go may run code that adds
	// to this very vector, and it must find the vector whole, with empty room past its end.
	std::vector<Slot> gone;
	if (held(kind)) {
		gone.assign(p_vector->data + p_length, p_vector->data + old);
	}
	std::fill(p_vector->data + p_length, p_vector->data + old, Slot());
	p_vector->length = p_length;
	for (Slot &each : gone) {
		drop(kind, each);
	}
	return true;
}

// ArrayNew, and VecNew: a vector of the length asked for, fixed or not.
void Interpreter::vector_new(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	Slot *r = p_registers;
	if (p_in.op == Op::ArrayNew) {
		array_new(p_function, p_in);
		return;
	}
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const uint32_t length = r[list[0]].u;
	Vector *made = length <= MAX_VECTOR_LENGTH ? new_vector(p_in.b, length, r[list[1]].b, collector_.list_for(p_in.b)) : nullptr;
	if (!made) {
		out_of_memory();
		return;
	}
	assign(Kind::Obj, r[p_in.a], made);
}

// VecGet at an index that names no element: of an Array, the property named by the number; of a Vector,
// AS3's RangeError.
void Interpreter::get_outside(Vector *p_vector, double p_index, Slot &r_out) {
	if (module_.classes[p_vector->klass].array) {
		array_number_get(p_vector, p_index, r_out);
		return;
	}
	raise_range(p_index, p_vector->length);
}

// VecSet at an index that names no element and is not the one past the end that a Vector may add.
void Interpreter::set_outside(Vector *p_vector, Kind p_index_kind, const Slot &p_index, Slot p_value) {
	const bool array = module_.classes[p_vector->klass].array;
	if (array && !element_index(p_index_kind, p_index, UINT32_MAX)) {
		array_number_set(p_vector, index_number(p_index_kind, p_index), p_value);
		return;
	}
	if (array) {
		out_of_memory(); // An index past the most elements an Array may hold here.
		return;
	}
	raise_range(index_number(p_index_kind, p_index), p_vector->length);
}

void Interpreter::vector_op(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	Slot *r = p_registers;
	if (p_in.op == Op::ArrayNew || p_in.op == Op::VecNew) {
		vector_new(p_function, p_in, p_registers);
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
			const std::optional<uint32_t> at = element_index(index, r[p_in.c], vector->length);
			if (!at) {
				get_outside(vector, index_number(index, r[p_in.c]), r[p_in.a]);
				return;
			}
			put(kind, r[p_in.a], share(kind, vector->data[*at]));
			break;
		}
		case Op::VecSet: {
			// One past the end adds an element, unless the length is fixed. An Array grows to take any index,
			// the elements between undefined.
			const Kind index = p_function.registers[p_in.b];
			const bool array = module_.classes[vector->klass].array;
			const uint32_t limit = array ? MAX_VECTOR_LENGTH : vector->fixed ? vector->length : vector->length + 1;
			const std::optional<uint32_t> at = element_index(index, r[p_in.b], limit);
			if (!at) {
				set_outside(vector, index, r[p_in.b], r[p_in.c]);
				return;
			}
			if (*at >= vector->length && !resize_vector(vector, *at + 1)) {
				return;
			}
			put(kind, vector->data[*at], share(kind, r[p_in.c]));
			break;
		}
		case Op::VecLength:
			set_uint32(r[p_in.a], vector->length);
			break;
		case Op::VecSetLength:
			// A fixed vector's length can't be set at all, even to what it is (AVM2 checks first).
			if (!length_is_fixed(vector) && r[p_in.b].u != vector->length) {
				resize_vector(vector, r[p_in.b].u);
			}
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

// VecFrom, Vector.<T>(x): x itself if it is one. Otherwise a new one, as long as an Array or another
// Vector, of its elements, each converted as a store into the vector converts it (which may run the
// program: an element's text). Anything else, null included, is AS3's TypeError #1034 (avmplus's
// VectorClass, from recollection).
void Interpreter::vector_from(const Instruction &p_in) {
	const size_t base = frames_.back().base;
	const Slot source = share(Kind::Any, stack_[base + p_in.b]);
	Object *object = held_object(source);
	if (object && object->klass == p_in.c) {
		put(Kind::Obj, stack_[base + p_in.a], share(Kind::Obj, of_object(object)));
		drop_any(source);
		return;
	}
	const Kind given = object ? module_.classes[object->klass].element : Kind::Void;
	if (given == Kind::Void) {
		coercion_failed(source, Kind::Obj, p_in.c);
		drop_any(source);
		return;
	}
	const Vector *from = static_cast<const Vector *>(object);
	const ir::Class &klass = module_.classes[p_in.c];
	Vector *made = fresh_vector(p_in.c, from->length);
	bool ok = made != nullptr;
	for (uint32_t i = 0; ok && i < made->length; i++) {
		// One taken out of the source as an earlier element spoke is undefined.
		const Slot element = i < from->length ? to_any(given, from->data[i]) : any::undefined();
		ok = from_any(element, klass.element, klass.element_check, &made->data[i]);
		drop_any(element);
	}
	if (ok) {
		put(Kind::Obj, stack_[frames_.back().base + p_in.a], of_object(made));
	} else if (made) {
		release_object(made);
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
