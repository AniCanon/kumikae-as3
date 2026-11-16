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

// A list of slots taken from the interpreter's spares and given back, emptied, when it goes: a call through
// a function value allocates nothing once the spares have grown. One per call in progress, since making an
// argument its type may run the program, which may make such a call again.
class SpareSlots {
public:
	explicit SpareSlots(std::vector<std::vector<Slot>> &p_spares) :
			spares_(p_spares) {
		if (!spares_.empty()) {
			slots_ = std::move(spares_.back());
			spares_.pop_back();
		}
	}
	~SpareSlots() {
		slots_.clear();
		spares_.push_back(std::move(slots_));
	}
	SpareSlots(const SpareSlots &) = delete;
	SpareSlots &operator=(const SpareSlots &) = delete;
	std::vector<Slot> &operator*() { return slots_; }
	std::vector<Slot> *operator->() { return &slots_; }

private:
	std::vector<std::vector<Slot>> &spares_;
	std::vector<Slot> slots_;
};

} // namespace vm
} // namespace kas3
