// What the interpreter's own files share. Nothing outside vm/interpreter*.cpp includes this.
#pragma once

#include "interpreter.h"
#include "numeric.h"

namespace kas3 {
namespace vm {

// A value the interpreter must give up when its register, field or global is overwritten: a counted
// string or object, or a handle on an engine value.
inline bool held(ir::Kind p_kind) {
	return ir::is_reference(p_kind) || ir::is_engine(p_kind);
}

// A Boolean into a register. The whole slot is cleared first: a register is compared and copied as 64
// bits, and a Boolean fills one of them.
inline void set_bool(Slot &r_slot, bool p_value) {
	r_slot.bits = 0;
	r_slot.b = p_value;
}

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
