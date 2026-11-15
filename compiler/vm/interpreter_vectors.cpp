#include "interpreter_internal.h"

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

void Interpreter::vector_op(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	Slot *r = p_registers;
	if (p_in.op == Op::VecNew) {
		const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
		const uint32_t length = r[list[0]].u;
		Vector *made = length <= MAX_VECTOR_LENGTH ? new_vector(p_in.b, length, r[list[1]].b) : nullptr;
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
			const Kind index = p_function.registers[p_in.c];
			const std::optional<uint32_t> at = element_index(index, r[p_in.c], vector->length);
			if (!at) {
				raise_range(index_number(index, r[p_in.c]), vector->length);
				return;
			}
			put(kind, r[p_in.a], share(kind, vector->data[*at]));
			break;
		}
		case Op::VecSet: {
			// One past the end adds an element, unless the length is fixed.
			const Kind index = p_function.registers[p_in.b];
			const std::optional<uint32_t> at = element_index(index, r[p_in.b], vector->fixed ? vector->length : vector->length + 1);
			if (!at) {
				raise_range(index_number(index, r[p_in.b]), vector->length);
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
