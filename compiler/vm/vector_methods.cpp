#include "interpreter_internal.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::VectorMethod;

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

} // namespace

// What a method gives back goes to the register that wants it, or is let go if nothing does.
void Interpreter::give(const VectorCall &p_call, Kind p_kind, Slot p_owned) {
	if (p_call.out == NONE) {
		drop(p_kind, p_owned);
	} else {
		put(p_kind, stack_[p_call.base + p_call.out], p_owned);
	}
}

bool Interpreter::length_is_fixed(const Vector *p_vector) {
	if (p_vector->fixed) {
		raise(module_.range_error_class, "Error #1126: Cannot change the length of a fixed Vector.");
	}
	return p_vector->fixed;
}

Vector *Interpreter::fresh_vector(uint32_t p_class, uint64_t p_length) {
	Vector *fresh = p_length <= MAX_VECTOR_LENGTH ? new_vector(p_class, static_cast<uint32_t>(p_length), false, collector_.list_for(p_class)) : nullptr;
	if (!fresh) {
		out_of_memory();
	}
	return fresh;
}

namespace {

Slot of_pointer(void *p_value) {
	Slot slot;
	slot.bits = 0;
	slot.p = p_value;
	return slot;
}

Slot of_number(uint32_t p_value) {
	Slot slot;
	slot.bits = 0;
	slot.u = p_value;
	return slot;
}

} // namespace

// Whether an element is the value looked for. One of Godot's values or objects is the same as Godot says:
// the same object, or an equal value. Null is only null.
bool Interpreter::element_is(Kind p_kind, Slot p_element, Slot p_value) {
	if (p_kind == Kind::Any) {
		bool equal = false;
		return any_equal(p_element, p_value, true, &equal) && equal;
	}
	if (!is_engine(p_kind)) {
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

void Interpreter::vector_call(const Function &p_function, const Instruction &p_in) {
	VectorCall call{ p_function.argument_lists[p_in.c], frames_.back().base, p_in.a, static_cast<VectorMethod>(p_in.b) };
	Vector *vector = static_cast<Vector *>(argument(call, 0).p);
	if (!vector) {
		raise_null();
		return;
	}
	const uint32_t length = vector->length;
	switch (call.method) {
		case VectorMethod::Push:
		case VectorMethod::Unshift:
		case VectorMethod::InsertAt:
			vector_add(vector, call);
			break;
		case VectorMethod::Pop:
		case VectorMethod::Shift:
		case VectorMethod::RemoveAt:
			vector_take(vector, call);
			break;
		case VectorMethod::IndexOf:
		case VectorMethod::LastIndexOf:
			vector_find(vector, call);
			break;
		case VectorMethod::Reverse:
			std::reverse(vector->data, vector->data + length);
			give(call, Kind::Obj, share(Kind::Obj, of_pointer(vector)));
			break;
		case VectorMethod::Slice:
		case VectorMethod::Concat:
			vector_copy(vector, call);
			break;
		case VectorMethod::Join:
			vector_join(vector, call);
			break;
		case VectorMethod::Splice:
			splice(vector, call);
			break;
		case VectorMethod::Sort:
			vector_sort(vector, call);
			break;
		case VectorMethod::SortOn:
			vector_sort_on(vector, call);
			break;
	}
}

// indexOf and lastIndexOf: the index of the first element, from the start or from the end, that is the
// value, or -1.
void Interpreter::vector_find(Vector *p_vector, const VectorCall &p_call) {
	const Kind kind = module_.classes[p_vector->klass].element;
	const uint32_t length = p_vector->length;
	int32_t found = -1;
	if (p_call.method == VectorMethod::IndexOf) {
		for (uint32_t i = clamped(argument(p_call, 2).i, length); i < length && found < 0; i++) {
			found = element_is(kind, p_vector->data[i], argument(p_call, 1)) ? static_cast<int32_t>(i) : -1;
		}
	} else {
		// From the end if negative; before the start, there is nothing to look at.
		const int32_t asked = argument(p_call, 2).i;
		int64_t from = asked < 0 ? static_cast<int64_t>(length) + asked : asked;
		from = from >= length ? static_cast<int64_t>(length) - 1 : from;
		for (int64_t i = from; i >= 0 && found < 0; i--) {
			found = element_is(kind, p_vector->data[i], argument(p_call, 1)) ? static_cast<int32_t>(i) : -1;
		}
	}
	give(p_call, Kind::I32, of_number(static_cast<uint32_t>(found)));
}

// join: the elements as text, with the separator given between them.
void Interpreter::vector_join(Vector *p_vector, const VectorCall &p_call) {
	const Str *separator = static_cast<const Str *>(argument(p_call, 1).p);
	const std::string between(separator ? separator->view() : std::string_view("null"));
	std::string all;
	retain(p_vector); // Kept while its elements speak for themselves, which may run the program.
	const bool done = vector_text(p_vector, between, &all);
	release_object(p_vector);
	if (done) {
		give(p_call, Kind::Str, of_pointer(text(all)));
	}
}

// push and unshift (any number of elements, giving the new length) and insertAt (one, at an index).
void Interpreter::vector_add(Vector *p_vector, const VectorCall &p_call) {
	const Kind kind = module_.classes[p_vector->klass].element;
	const uint32_t length = p_vector->length;
	const bool insert = p_call.method == VectorMethod::InsertAt;
	const uint32_t first = insert ? 2 : 1; // Where the elements start among the arguments.
	const uint32_t added = static_cast<uint32_t>(p_call.list.size()) - first;
	const uint32_t at = insert ? clamped(argument(p_call, 1).i, length) : p_call.method == VectorMethod::Unshift ? 0 : length;
	if (added && (length_is_fixed(p_vector) || !resize_vector(p_vector, length + added))) {
		return;
	}
	if (added) {
		shift_slots(p_vector, at, length - at, added);
		std::fill(p_vector->data + at, p_vector->data + at + added, Slot());
	}
	for (uint32_t i = 0; i < added; i++) {
		put(kind, p_vector->data[at + i], share(kind, argument(p_call, first + i)));
	}
	if (!insert) {
		give(p_call, Kind::U32, of_number(p_vector->length));
	}
}

// pop, shift and removeAt: one element out, given to whoever asked.
void Interpreter::vector_take(Vector *p_vector, const VectorCall &p_call) {
	const Kind kind = module_.classes[p_vector->klass].element;
	const uint32_t length = p_vector->length;
	if (length_is_fixed(p_vector)) {
		return;
	}
	int64_t at = p_call.method == VectorMethod::Pop ? static_cast<int64_t>(length) - 1 : 0;
	if (p_call.method == VectorMethod::RemoveAt) {
		const int32_t asked = argument(p_call, 1).i;
		at = asked < 0 ? static_cast<int64_t>(length) + asked : asked;
		if (at < 0 || at >= length) {
			raise_range(asked, length);
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
	give(p_call, kind, element);
}

// slice and concat: a new vector that shares the elements.
void Interpreter::vector_copy(Vector *p_vector, const VectorCall &p_call) {
	const Kind kind = module_.classes[p_vector->klass].element;
	const uint32_t length = p_vector->length;
	if (p_call.method == VectorMethod::Slice) {
		const uint32_t start = clamped(argument(p_call, 1).i, length);
		const uint32_t end = std::max(start, clamped(argument(p_call, 2).i, length));
		Vector *part = fresh_vector(p_vector->klass, end - start);
		for (uint32_t i = start; part && i < end; i++) {
			part->data[i - start] = share(kind, p_vector->data[i]);
		}
		if (part) {
			give(p_call, Kind::Obj, of_pointer(part));
		}
		return;
	}
	if (module_.classes[p_vector->klass].array) {
		array_concat(p_vector, p_call);
		return;
	}
	uint64_t total = 0;
	for (size_t i = 0; i < p_call.list.size(); i++) {
		const Vector *other = static_cast<const Vector *>(argument(p_call, i).p);
		if (!other) {
			raise_null();
			return;
		}
		total += other->length;
	}
	Vector *all = fresh_vector(p_vector->klass, total);
	if (!all) {
		return;
	}
	uint32_t at = 0;
	for (size_t i = 0; i < p_call.list.size(); i++) {
		const Vector *from = static_cast<const Vector *>(argument(p_call, i).p);
		for (uint32_t k = 0; k < from->length; k++) {
			all->data[at++] = share(kind, from->data[k]);
		}
	}
	give(p_call, Kind::Obj, of_pointer(all));
}

// Array's concat: the array's elements, then each argument's: an Array's elements (one level), anything
// else as itself.
void Interpreter::array_concat(Vector *p_array, const VectorCall &p_call) {
	const auto spread = [&](Slot p_value) -> const Vector * {
		const Object *object = any::tag(p_value) == AnyTag::Object ? static_cast<const Object *>(any::pointer(p_value)) : nullptr;
		return object && module_.classes[object->klass].array ? static_cast<const Vector *>(object) : nullptr;
	};
	uint64_t total = p_array->length;
	for (size_t i = 1; i < p_call.list.size(); i++) {
		const Vector *other = spread(argument(p_call, i));
		total += other ? other->length : 1;
	}
	Vector *all = fresh_vector(p_array->klass, total);
	if (!all) {
		return;
	}
	uint32_t at = 0;
	for (uint32_t k = 0; k < p_array->length; k++) {
		all->data[at++] = share(Kind::Any, p_array->data[k]);
	}
	for (size_t i = 1; i < p_call.list.size(); i++) {
		const Slot value = argument(p_call, i);
		if (const Vector *other = spread(value)) {
			for (uint32_t k = 0; k < other->length; k++) {
				all->data[at++] = share(Kind::Any, other->data[k]);
			}
		} else {
			all->data[at++] = share(Kind::Any, value);
		}
	}
	give(p_call, Kind::Obj, of_pointer(all));
}

// splice(start, count, ...items): takes `count` elements out from `start`, puts the items in their place,
// and gives back what was taken out.
void Interpreter::splice(Vector *p_vector, const VectorCall &p_call) {
	const Kind kind = module_.classes[p_vector->klass].element;
	const uint32_t length = p_vector->length;
	const uint32_t start = clamped(argument(p_call, 1).i, length);
	const uint32_t taken = std::min(argument(p_call, 2).u, length - start);
	const uint32_t added = static_cast<uint32_t>(p_call.list.size()) - 3;
	if (taken != added && length_is_fixed(p_vector)) {
		return;
	}
	const uint64_t grown = static_cast<uint64_t>(length) - taken + added;
	if (grown > MAX_VECTOR_LENGTH || !vector_reserve(p_vector, static_cast<uint32_t>(grown))) {
		out_of_memory();
		return;
	}
	Vector *removed = fresh_vector(p_vector->klass, taken);
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
		p_vector->data[start + i] = share(kind, argument(p_call, 3 + i));
	}
	give(p_call, Kind::Obj, of_pointer(removed));
}

} // namespace vm
} // namespace kas3
