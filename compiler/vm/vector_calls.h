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

// The element an index names, if it is a whole number from 0 to p_limit - 1.
inline std::optional<uint32_t> element_index(double p_index, uint32_t p_limit) {
	if (!(p_index >= 0 && p_index < static_cast<double>(p_limit))) {
		return std::nullopt;
	}
	const uint32_t whole = static_cast<uint32_t>(p_index);
	return static_cast<double>(whole) == p_index ? std::optional<uint32_t>(whole) : std::nullopt;
}

inline std::optional<uint32_t> element_index(ir::Kind p_kind, Slot p_index, uint32_t p_limit) {
	if (p_kind == ir::Kind::I32) {
		return p_index.i >= 0 && static_cast<uint32_t>(p_index.i) < p_limit ? std::optional<uint32_t>(static_cast<uint32_t>(p_index.i)) : std::nullopt;
	}
	if (p_kind == ir::Kind::U32 || p_kind == ir::Kind::U8) {
		return p_index.u < p_limit ? std::optional<uint32_t>(p_index.u) : std::nullopt;
	}
	return element_index(p_index.d, p_limit);
}

void vector_sort(Library &p_library, Vector *p_vector, const VectorCall &p_call);
void vector_sort_on(Library &p_library, Vector *p_vector, const VectorCall &p_call);

} // namespace vm
} // namespace kas3
