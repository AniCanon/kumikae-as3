// A String's methods that only read text, as functions of plain values: nothing of the interpreter, its
// frames or its registers. The interpreter's StrCall, a call by name and native code all run these.
#pragma once

#include "ir.h"
#include "runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace kas3 {
namespace vm {

// The values a method is handed, in the order StrCall lists them (the String first, for a method of one):
// slots as they lie, or, with `order`, the slots of those numbers (a frame's registers and an
// instruction's list of them).
struct Values {
	const Slot *slots = nullptr;
	const uint32_t *order = nullptr;
	size_t count = 0;
	const Slot &operator[](size_t p_index) const { return order ? slots[order[p_index]] : slots[p_index]; }
};

// A String of one character for each of the first 128, made once and kept by whoever holds the array.
using Characters = std::array<Str *, 128>;

// One of the methods ir::reads_text_only() names, done. What it gives is written to r_out whole: a Number,
// an int, or a String with one
// reference, the caller's. False, with nothing written, if memory ran out. p_characters may be null.
bool text_method(ir::StringFunction p_function, const Values &p_values, Characters *p_characters, Slot *r_out);

} // namespace vm
} // namespace kas3
