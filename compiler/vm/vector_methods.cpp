// A vector's and an Array's methods, as functions of plain values (vm/library.h).
#include "vector_calls.h"

#include "any.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace kas3 {
namespace vm {

using ir::Kind;
using ir::VectorMethod;

namespace {

void array_concat(Library &p_library, Vector *p_array, const VectorCall &p_call);

// An index as slice() and splice() take it: from the end if negative, and never outside the vector.
uint32_t clamped(int32_t p_index, uint32_t p_length) {
	if (p_index < 0) {
		const int64_t from_end = static_cast<int64_t>(p_length) + p_index;
		return from_end < 0 ? 0 : static_cast<uint32_t>(from_end);
	}
	return static_cast<uint32_t>(p_index) > p_length ? p_length : static_cast<uint32_t>(p_index);
}

// The slots from p_from on move by p_by places, up or down. Nothing is retained or released: each
// reference is still held once.
void shift_slots(Vector *p_vector, uint32_t p_from, uint32_t p_count, int64_t p_by) {
	std::memmove(static_cast<void *>(p_vector->data + p_from + p_by), static_cast<const void *>(p_vector->data + p_from), p_count * sizeof(Slot));
}

Slot of_number(uint32_t p_value) {
	Slot slot;
	slot.bits = 0;
	slot.u = p_value;
	return slot;
}

// indexOf and lastIndexOf: the index of the first element, from the start or from the end, that is the
// value, or -1.
void vector_find(Library &p_library, Vector *p_vector, const VectorCall &p_call) {
	const Kind kind = p_library.module().classes[p_vector->klass].element;
	const uint32_t length = p_vector->length;
	int32_t found = -1;
	if (p_call.method == VectorMethod::IndexOf) {
		for (uint32_t i = clamped(p_call.values[2].i, length); i < length && found < 0; i++) {
			found = p_library.unmoved.element_is(kind, p_vector->data[i], p_call.values[1]) ? static_cast<int32_t>(i) : -1;
		}
	} else {
		// From the end if negative; before the start, there is nothing to look at.
		const int32_t asked = p_call.values[2].i;
		int64_t from = asked < 0 ? static_cast<int64_t>(length) + asked : asked;
		from = from >= length ? static_cast<int64_t>(length) - 1 : from;
		for (int64_t i = from; i >= 0 && found < 0; i--) {
			found = p_library.unmoved.element_is(kind, p_vector->data[i], p_call.values[1]) ? static_cast<int32_t>(i) : -1;
		}
	}
	give(p_library, p_call, Kind::I32, of_number(static_cast<uint32_t>(found)));
}

// join: the elements as text, with the separator given between them. toLocaleString(): each element in
// its locale words, with commas between.
void vector_join(Library &p_library, Vector *p_vector, const VectorCall &p_call) {
	const bool locale = p_call.method == VectorMethod::LocaleText;
	const Str *separator = locale ? nullptr : static_cast<const Str *>(p_call.values[1].p);
	const std::string between(locale ? std::string_view(",") : separator ? separator->view() : std::string_view("null"));
	std::string all;
	retain(p_vector); // Kept while its elements speak for themselves, which may run the program.
	const bool done = p_library.unmoved.vector_text(p_vector, between, &all, locale);
	p_library.heap.release_object(p_vector);
	if (done) {
		give(p_library, p_call, Kind::Str, of_pointer(p_library.text(all)));
	}
}

// push and unshift (any number of elements, giving the new length) and insertAt (one, at an index).
void vector_add(Library &p_library, Vector *p_vector, const VectorCall &p_call) {
	const Kind kind = p_library.module().classes[p_vector->klass].element;
	const uint32_t length = p_vector->length;
	const bool insert = p_call.method == VectorMethod::InsertAt;
	const uint32_t first = insert ? 2 : 1; // Where the elements start among the arguments.
	const uint32_t added = static_cast<uint32_t>(p_call.count) - first;
	const uint32_t at = insert ? clamped(p_call.values[1].i, length) : p_call.method == VectorMethod::Unshift ? 0 : length;
	if (added && (length_is_fixed(p_library, p_vector) || !resize_vector(p_library, p_vector, length + added))) {
		return;
	}
	if (added) {
		shift_slots(p_vector, at, length - at, added);
		std::fill(p_vector->data + at, p_vector->data + at + added, Slot());
	}
	for (uint32_t i = 0; i < added; i++) {
		p_library.heap.put(kind, p_vector->data[at + i], p_library.heap.share(kind, p_call.values[first + i]));
	}
	if (!insert) {
		give(p_library, p_call, Kind::U32, of_number(p_vector->length));
	}
}

// pop, shift and removeAt: one element out, given to whoever asked.
void vector_take(Library &p_library, Vector *p_vector, const VectorCall &p_call) {
	const Kind kind = p_library.module().classes[p_vector->klass].element;
	const uint32_t length = p_vector->length;
	if (length_is_fixed(p_library, p_vector)) {
		return;
	}
	int64_t at = p_call.method == VectorMethod::Pop ? static_cast<int64_t>(length) - 1 : 0;
	if (p_call.method == VectorMethod::RemoveAt) {
		const int32_t asked = p_call.values[1].i;
		at = asked < 0 ? static_cast<int64_t>(length) + asked : asked;
		if (at < 0 || at >= length) {
			raise_range(p_library, asked, length);
			return;
		}
	}
	// From an empty vector: undefined, made the element type, as AVM2 does: NaN for a Number, 0 for the
	// other numbers, false, null for the rest, and undefined itself for untyped elements.
	Slot element = empty_value(kind == Kind::F64, false);
	if (length != 0) {
		element = p_vector->data[at];
		shift_slots(p_vector, static_cast<uint32_t>(at) + 1, length - static_cast<uint32_t>(at) - 1, -1);
		p_vector->data[length - 1].bits = 0;
		p_vector->length = length - 1;
	}
	give(p_library, p_call, kind, element);
}

// slice and concat: a new vector that shares the elements.
void vector_copy(Library &p_library, Vector *p_vector, const VectorCall &p_call) {
	const Kind kind = p_library.module().classes[p_vector->klass].element;
	const uint32_t length = p_vector->length;
	if (p_call.method == VectorMethod::Slice) {
		const uint32_t start = clamped(p_call.values[1].i, length);
		const uint32_t end = std::max(start, clamped(p_call.values[2].i, length));
		Vector *part = p_library.fresh_vector(p_vector->klass, end - start);
		for (uint32_t i = start; part && i < end; i++) {
			part->data[i - start] = p_library.heap.share(kind, p_vector->data[i]);
		}
		if (part) {
			give(p_library, p_call, Kind::Obj, of_pointer(part));
		}
		return;
	}
	if (p_library.module().classes[p_vector->klass].array) {
		array_concat(p_library, p_vector, p_call);
		return;
	}
	uint64_t total = 0;
	for (size_t i = 0; i < p_call.count; i++) {
		const Vector *other = static_cast<const Vector *>(p_call.values[i].p);
		if (!other) {
			p_library.raise_null();
			return;
		}
		total += other->length;
	}
	Vector *all = p_library.fresh_vector(p_vector->klass, total);
	if (!all) {
		return;
	}
	uint32_t at = 0;
	for (size_t i = 0; i < p_call.count; i++) {
		const Vector *from = static_cast<const Vector *>(p_call.values[i].p);
		for (uint32_t k = 0; k < from->length; k++) {
			all->data[at++] = p_library.heap.share(kind, from->data[k]);
		}
	}
	give(p_library, p_call, Kind::Obj, of_pointer(all));
}

// Array's concat: the array's elements, then each argument's: an Array's elements (one level), anything
// else as itself.
void array_concat(Library &p_library, Vector *p_array, const VectorCall &p_call) {
	const auto spread = [&](Slot p_value) -> const Vector * {
		const Object *object = any::tag(p_value) == AnyTag::Object ? static_cast<const Object *>(any::pointer(p_value)) : nullptr;
		return object && p_library.module().classes[object->klass].array ? static_cast<const Vector *>(object) : nullptr;
	};
	uint64_t total = p_array->length;
	for (size_t i = 1; i < p_call.count; i++) {
		const Vector *other = spread(p_call.values[i]);
		total += other ? other->length : 1;
	}
	Vector *all = p_library.fresh_vector(p_array->klass, total);
	if (!all) {
		return;
	}
	uint32_t at = 0;
	for (uint32_t k = 0; k < p_array->length; k++) {
		all->data[at++] = p_library.heap.share(Kind::Any, p_array->data[k]);
	}
	for (size_t i = 1; i < p_call.count; i++) {
		const Slot value = p_call.values[i];
		if (const Vector *other = spread(value)) {
			for (uint32_t k = 0; k < other->length; k++) {
				all->data[at++] = p_library.heap.share(Kind::Any, other->data[k]);
			}
		} else {
			all->data[at++] = p_library.heap.share(Kind::Any, value);
		}
	}
	give(p_library, p_call, Kind::Obj, of_pointer(all));
}

// splice(start, count, ...items): takes `count` elements out from `start`, puts the items in their place,
// and gives back what was taken out.
void splice(Library &p_library, Vector *p_vector, const VectorCall &p_call) {
	const Kind kind = p_library.module().classes[p_vector->klass].element;
	const uint32_t length = p_vector->length;
	const uint32_t start = clamped(p_call.values[1].i, length);
	const uint32_t taken = std::min(p_call.values[2].u, length - start);
	const uint32_t added = static_cast<uint32_t>(p_call.count) - 3;
	if (taken != added && length_is_fixed(p_library, p_vector)) {
		return;
	}
	const uint64_t grown = static_cast<uint64_t>(length) - taken + added;
	if (grown > MAX_VECTOR_LENGTH || !vector_reserve(p_vector, static_cast<uint32_t>(grown))) {
		p_library.runner.out_of_memory();
		return;
	}
	Vector *removed = p_library.fresh_vector(p_vector->klass, taken);
	if (!removed) {
		return;
	}
	// What is taken out moves to the new vector as it is: each reference is still held once.
	std::memcpy(static_cast<void *>(removed->data), static_cast<const void *>(p_vector->data + start), taken * sizeof(Slot));
	shift_slots(p_vector, start + taken, length - start - taken, static_cast<int64_t>(added) - taken);
	if (grown < length) {
		std::fill(p_vector->data + grown, p_vector->data + length, Slot());
	}
	p_vector->length = static_cast<uint32_t>(grown);
	for (uint32_t i = 0; i < added; i++) {
		p_vector->data[start + i] = p_library.heap.share(kind, p_call.values[3 + i]);
	}
	give(p_library, p_call, Kind::Obj, of_pointer(removed));
}

} // namespace

bool vector_call(Library &p_library, VectorMethod p_method, const Slot *p_values, size_t p_count, Slot *r_out) {
	const VectorCall call{ p_values, p_count, r_out, p_method };
	Vector *vector = static_cast<Vector *>(p_values[0].p);
	if (!vector) {
		p_library.raise_null();
		return false;
	}
	switch (p_method) {
		case VectorMethod::Push:
		case VectorMethod::Unshift:
		case VectorMethod::InsertAt:
			vector_add(p_library, vector, call);
			break;
		case VectorMethod::Pop:
		case VectorMethod::Shift:
		case VectorMethod::RemoveAt:
			vector_take(p_library, vector, call);
			break;
		case VectorMethod::IndexOf:
		case VectorMethod::LastIndexOf:
			vector_find(p_library, vector, call);
			break;
		case VectorMethod::Reverse:
			std::reverse(vector->data, vector->data + vector->length);
			give(p_library, call, Kind::Obj, p_library.heap.share(Kind::Obj, of_pointer(vector)));
			break;
		case VectorMethod::Slice:
		case VectorMethod::Concat:
			vector_copy(p_library, vector, call);
			break;
		case VectorMethod::Join:
		case VectorMethod::LocaleText:
			vector_join(p_library, vector, call);
			break;
		case VectorMethod::Splice:
			splice(p_library, vector, call);
			break;
		case VectorMethod::Sort:
			vector_sort(p_library, vector, call);
			break;
		case VectorMethod::SortOn:
			vector_sort_on(p_library, vector, call);
			break;
	}
	return !p_library.runner.failing();
}

} // namespace vm
} // namespace kas3
