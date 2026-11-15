#include "interpreter_internal.h"

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

// More elements than this is refused: a length that came from a negative number, most likely.
const uint32_t MAX_LENGTH = 1u << 30;

// The element an index names, if it is a whole number from 0 to p_limit - 1.
std::optional<uint32_t> element_index(double p_index, uint32_t p_limit) {
	if (!(p_index >= 0 && p_index < static_cast<double>(p_limit))) {
		return std::nullopt;
	}
	const uint32_t whole = static_cast<uint32_t>(p_index);
	return static_cast<double>(whole) == p_index ? std::optional<uint32_t>(whole) : std::nullopt;
}

} // namespace

void Interpreter::raise_range(double p_index, uint32_t p_length) {
	raise(module_.range_error_class,
			"Error #1125: The index " + number_to_string(p_index) + " is out of range " + std::to_string(p_length) + ".");
}

// Gives the vector the length asked for: elements past it are let go, and new ones are empty.
bool Interpreter::resize_vector(Vector *p_vector, uint32_t p_length) {
	if (p_vector->fixed) {
		raise(module_.range_error_class, "Error #1126: Cannot change the length of a fixed Vector.");
		return false;
	}
	if (p_length > MAX_LENGTH || !vector_reserve(p_vector, p_length)) {
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

void Interpreter::vector_op(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	Slot *r = p_registers;
	if (p_in.op == Op::VecNew) {
		const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
		const uint32_t length = r[list[0]].u;
		Vector *made = length <= MAX_LENGTH ? new_vector(p_in.b, length, r[list[1]].b) : nullptr;
		if (!made) {
			out_of_memory();
			return;
		}
		assign(Kind::Obj, r[p_in.a], made);
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
			const std::optional<uint32_t> at = element_index(r[p_in.c].d, vector->length);
			if (!at) {
				raise_range(r[p_in.c].d, vector->length);
				return;
			}
			put(kind, r[p_in.a], share(kind, vector->data[*at]));
			break;
		}
		case Op::VecSet: {
			// One past the end adds an element, unless the length is fixed.
			const std::optional<uint32_t> at = element_index(r[p_in.b].d, vector->fixed ? vector->length : vector->length + 1);
			if (!at) {
				raise_range(r[p_in.b].d, vector->length);
				return;
			}
			if (*at == vector->length && !resize_vector(vector, vector->length + 1)) {
				return;
			}
			put(kind, vector->data[*at], share(kind, r[p_in.c]));
			break;
		}
		case Op::VecLength:
			set_uint32(r[p_in.a], vector->length);
			break;
		case Op::VecSetLength:
			if (r[p_in.b].u != vector->length) {
				resize_vector(vector, r[p_in.b].u);
			}
			break;
		case Op::VecFixed:
			r[p_in.a].bits = 0;
			r[p_in.a].b = vector->fixed;
			break;
		case Op::VecSetFixed:
			vector->fixed = r[p_in.b].b;
			break;
		default:
			break;
	}
}

// The elements as text with p_separator between them, as join() and toString() give it. An element that
// is an object says what it is in its own words, which runs the program: false if that failed.
bool Interpreter::vector_text(Vector *p_vector, std::string_view p_separator, std::string *r_text) {
	const Kind kind = module_.classes[p_vector->klass].element;
	for (uint32_t i = 0; i < p_vector->length; i++) {
		if (i) {
			*r_text += p_separator;
		}
		const Slot element = p_vector->data[i];
		switch (kind) {
			case Kind::I32:
				*r_text += std::to_string(element.i);
				break;
			case Kind::U32:
				*r_text += std::to_string(element.u);
				break;
			case Kind::I64:
				*r_text += std::to_string(element.l);
				break;
			case Kind::F64:
				*r_text += number_to_string(element.d);
				break;
			case Kind::Bool:
				*r_text += element.b ? "true" : "false";
				break;
			case Kind::Str:
				*r_text += element.p ? static_cast<Str *>(element.p)->view() : std::string_view("null");
				break;
			case Kind::Obj: {
				// Held while it speaks: what it says may run the program, which may take it out of
				// this vector, and then nothing else would be holding it.
				Object *object = static_cast<Object *>(element.p);
				retain(object);
				const bool said = object_as_text(object, r_text);
				release_object(object);
				if (!said) {
					return false;
				}
				break;
			}
			default: {
				// A handle of its own, for the same reason: Godot's text for an object can run a script.
				void *held = element.p ? host_.handles.copy(element.p) : nullptr;
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
				break;
			}
		}
	}
	return true;
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
