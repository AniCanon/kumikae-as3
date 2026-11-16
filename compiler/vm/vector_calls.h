// What the files of a vector's methods share (vector_methods.cpp, vector_sort.cpp, vector_elements.cpp).
// Nothing outside them includes this: the library's own entries are in vm/library.h.
#pragma once

#include "library.h"

#include <optional>

namespace kas3 {
namespace vm {

// A call of one of a vector's methods: its values (the vector first), and where what it gives goes.
struct VectorCall {
	const Slot *values;
	size_t count;
	Slot *out; // Null where nobody wants what the method gives.
	ir::VectorMethod method;
	mutable ir::Kind gave = ir::Kind::Void; // The kind of what was written to `out`, once something was.
};

// What a method gives goes to whoever asked, or is let go of where nobody did.
inline void give(Library &p_library, const VectorCall &p_call, ir::Kind p_kind, Slot p_owned) {
	if (p_call.out) {
		*p_call.out = p_owned;
		p_call.gave = p_kind;
	} else {
		p_library.heap.drop(p_kind, p_owned);
	}
}

inline Slot of_pointer(void *p_value) {
	Slot slot;
	slot.bits = 0;
	slot.p = p_value;
	return slot;
}

// The element an index of that kind (an int, a uint or a Number) names, if it is a whole number below p_limit.
std::optional<uint32_t> element_index(ir::Kind p_kind, Slot p_index, uint32_t p_limit);

void vector_sort(Library &p_library, Vector *p_vector, const VectorCall &p_call);
void vector_sort_on(Library &p_library, Vector *p_vector, const VectorCall &p_call);

} // namespace vm
} // namespace kas3
