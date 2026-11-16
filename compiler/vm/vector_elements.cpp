// A vector's or an Array's elements and length, and the making of one, as functions of plain values
// (vm/library.h).
#include "library.h"

#include "any.h"
#include "vector_calls.h"

#include <algorithm>

namespace kas3 {
namespace vm {

using ir::Kind;

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
double index_number(Kind p_kind, Slot p_index) {
	return p_kind == Kind::I32 ? p_index.i : p_kind == Kind::U32 ? static_cast<double>(p_index.u) : p_index.d;
}

} // namespace

std::optional<uint32_t> element_index(Kind p_kind, Slot p_index, uint32_t p_limit) {
	if (p_kind == Kind::I32) {
		return p_index.i >= 0 && static_cast<uint32_t>(p_index.i) < p_limit ? std::optional<uint32_t>(static_cast<uint32_t>(p_index.i)) : std::nullopt;
	}
	if (p_kind == Kind::U32) {
		return p_index.u < p_limit ? std::optional<uint32_t>(p_index.u) : std::nullopt;
	}
	return element_index(p_index.d, p_limit);
}

void raise_range(Library &p_library, double p_index, uint32_t p_length) {
	p_library.runner.raise(p_library.module().range_error_class,
			"Error #1125: The index " + number_to_string(p_index) + " is out of range " + std::to_string(p_length) + ".");
}

bool length_is_fixed(Library &p_library, const Vector *p_vector) {
	if (p_vector->fixed) {
		p_library.runner.raise(p_library.module().range_error_class, "Error #1126: Cannot change the length of a fixed Vector.");
	}
	return p_vector->fixed;
}

// new Array(), Array(n), new Array(a, b): one argument that is a number is the length, which must be a whole
// number below 2^32 (AS3's RangeError #1005); anything else is the elements.
Vector *array_new(Library &p_library, uint32_t p_class, const Slot *p_values, size_t p_count) {
	const AnyTag tag = p_count == 1 ? any::tag(p_values[0]) : AnyTag::Undefined;
	uint64_t length = p_count;
	bool spread = true;
	if (tag == AnyTag::Int || tag == AnyTag::Number || tag == AnyTag::Wide) {
		const double asked = tag == AnyTag::Wide ? static_cast<double>(static_cast<Wide *>(any::pointer(p_values[0]))->value) : any::to_number(p_values[0]);
		if (!(asked >= 0 && asked <= 4294967295.0 && asked == static_cast<double>(static_cast<uint64_t>(asked)))) {
			p_library.runner.raise(p_library.module().range_error_class, "Error #1005: Array index is not a positive integer (" + number_to_string(asked) + ").");
			return nullptr;
		}
		length = static_cast<uint64_t>(asked);
		spread = false;
	}
	Vector *made = p_library.fresh_vector(p_class, length);
	for (size_t i = 0; made && spread && i < p_count; i++) {
		made->data[i] = p_library.heap.share(Kind::Any, p_values[i]);
	}
	return made;
}

// Gives the vector the length asked for: elements past it are let go, and new ones are empty.
bool resize_vector(Library &p_library, Vector *p_vector, uint32_t p_length) {
	if (length_is_fixed(p_library, p_vector)) {
		return false;
	}
	if (p_length > MAX_VECTOR_LENGTH || !vector_reserve(p_vector, p_length)) {
		p_library.runner.out_of_memory();
		return false;
	}
	const Kind kind = p_library.class_of(p_vector).element;
	const uint32_t old = p_vector->length;
	if (p_length >= old) {
		p_vector->length = p_length;
		return true;
	}
	// Every element that goes is taken out before any is let go: letting one go may run code that adds
	// to this very vector, and it must find the vector whole, with empty room past its end.
	std::vector<Slot> gone;
	if (ir::is_held(kind)) {
		gone.assign(p_vector->data + p_length, p_vector->data + old);
	}
	std::fill(p_vector->data + p_length, p_vector->data + old, Slot());
	p_vector->length = p_length;
	for (Slot &each : gone) {
		p_library.heap.drop(kind, each);
	}
	return true;
}

// The places of a vector from p_from to its end, empty until now, each given the zero of its Godot value type
// where the class says so (ir::Class::element_zero). False, with the error raised, if the engine can't make one.
bool fill_zeroes(Library &p_library, Vector *p_vector, uint32_t p_from) {
	const ir::Class &klass = p_library.class_of(p_vector);
	if (klass.element == Kind::Any && klass.element_check != ir::NONE) {
		// A Vector.<Object>: what an Object variable starts as, where a Vector.<*>'s place is undefined.
		std::fill(p_vector->data + p_from, p_vector->data + p_vector->length, any::null());
		return true;
	}
	if (!klass.element_zero) {
		return true;
	}
	for (uint32_t i = p_from; i < p_vector->length; i++) {
		std::string error;
		p_vector->data[i].p = p_library.heap.source.construct(klass.element_type, nullptr, 0, &error);
		if (!p_vector->data[i].p) {
			p_library.runner.raise(p_library.module().error_class, error.empty() ? "the engine refused" : error);
			return false;
		}
	}
	return true;
}

Vector *vector_new(Library &p_library, uint32_t p_class, uint32_t p_length, bool p_fixed) {
	Vector *made = p_length <= MAX_VECTOR_LENGTH ? new_vector(p_class, p_length, p_fixed, p_library.heap.list_for(p_class)) : nullptr;
	if (!made) {
		p_library.runner.out_of_memory();
		return nullptr;
	}
	if (!fill_zeroes(p_library, made, 0)) {
		p_library.heap.release_object(made);
		return nullptr;
	}
	return made;
}

bool vector_get(Library &p_library, Vector *p_vector, Kind p_index_kind, Slot p_index, Slot *r_out) {
	const Kind kind = p_library.class_of(p_vector).element;
	if (const std::optional<uint32_t> at = element_index(p_index_kind, p_index, p_vector->length)) {
		*r_out = p_library.heap.share(kind, p_vector->data[*at]);
		return true;
	}
	// An index that names no element: of an Array, the property named by the number; of a Vector, AS3's
	// RangeError.
	if (!p_library.class_of(p_vector).array) {
		raise_range(p_library, index_number(p_index_kind, p_index), p_vector->length);
		return false;
	}
	r_out->bits = 0;
	return p_library.unmoved.array_number_get(p_vector, index_number(p_index_kind, p_index), r_out);
}

bool vector_set(Library &p_library, Vector *p_vector, Kind p_index_kind, Slot p_index, Slot p_value) {
	// One past the end adds an element, unless the length is fixed. An Array grows to take any index, the
	// elements between undefined.
	const ir::Class &klass = p_library.class_of(p_vector);
	const uint32_t limit = klass.array ? MAX_VECTOR_LENGTH : p_vector->fixed ? p_vector->length : p_vector->length + 1;
	if (const std::optional<uint32_t> at = element_index(p_index_kind, p_index, limit)) {
		if (*at >= p_vector->length && !resize_vector(p_library, p_vector, *at + 1)) {
			return false;
		}
		p_library.heap.put(klass.element, p_vector->data[*at], p_library.heap.share(klass.element, p_value));
		return true;
	}
	if (klass.array && !element_index(p_index_kind, p_index, UINT32_MAX)) {
		return p_library.unmoved.array_number_set(p_vector, index_number(p_index_kind, p_index), p_value);
	}
	if (klass.array) {
		p_library.runner.out_of_memory(); // An index past the most elements an Array may hold here.
		return false;
	}
	raise_range(p_library, index_number(p_index_kind, p_index), p_vector->length);
	return false;
}

bool vector_set_length(Library &p_library, Vector *p_vector, uint32_t p_length) {
	// A fixed vector's length can't be set at all, even to what it is (AVM2 checks first).
	if (length_is_fixed(p_library, p_vector)) {
		return false;
	}
	const uint32_t old = p_vector->length;
	return p_length == old || (resize_vector(p_library, p_vector, p_length) && fill_zeroes(p_library, p_vector, old));
}

// Vector.<T>(x): x itself if it is one. Otherwise a new one, as long as an Array or another Vector, of its
// elements, each converted as a store into the vector converts it (which may run the program: an element's
// text). Anything else, null included, is AS3's TypeError #1034 (avmplus's VectorClass, from recollection).
bool vector_from(Library &p_library, uint32_t p_class, Slot p_source, Slot *r_out) {
	Object *object = any::tag(p_source) == AnyTag::Object ? static_cast<Object *>(any::pointer(p_source)) : nullptr;
	if (object && object->klass == p_class) {
		retain(object);
		r_out->bits = 0;
		r_out->p = object;
		return true;
	}
	const Kind given = object ? p_library.class_of(object).element : Kind::Void;
	if (given == Kind::Void) {
		return coercion_failed(p_library, p_source, Kind::Obj, p_class);
	}
	const Vector *from = static_cast<const Vector *>(object);
	const ir::Class &klass = p_library.module().classes[p_class];
	Vector *made = p_library.fresh_vector(p_class, from->length);
	bool ok = made != nullptr;
	for (uint32_t i = 0; ok && i < made->length; i++) {
		// One taken out of the source as an earlier element spoke is undefined.
		const Slot element = i < from->length ? to_any(p_library, given, from->data[i]) : any::undefined();
		ok = from_any(p_library, element, klass.element, klass.element_check, &made->data[i]);
		p_library.heap.drop_any(element);
	}
	if (!ok) {
		p_library.heap.release_object(made);
		return false;
	}
	r_out->bits = 0;
	r_out->p = made;
	return true;
}

} // namespace vm
} // namespace kas3
