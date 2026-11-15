// What the interpreter's own files share. Nothing outside vm/interpreter*.cpp includes this.
#pragma once

#include "interpreter.h"
#include "any.h"
#include "held.h"
#include "numeric.h"

namespace kas3 {
namespace vm {

// A value the interpreter must give up when its register, field or global is overwritten: a counted
// string or object, or a handle on an engine value.
inline bool held(ir::Kind p_kind) {
	return ir::is_held(p_kind);
}

// An integer into a register. The whole slot is cleared first: a register is compared and copied as 64
// bits, and a 32-bit integer fills half of them.
inline void set_int32(Slot &r_slot, int32_t p_value) {
	r_slot.bits = 0;
	r_slot.i = p_value;
}

inline void set_uint32(Slot &r_slot, uint32_t p_value) {
	r_slot.bits = 0;
	r_slot.u = p_value;
}

} // namespace vm
} // namespace kas3
