// A module whose functions have their bodies as native code (the C++ backend's): each such function's code
// is one Native instruction, and the interpreter asks whoever holds the native code to carry it out. The
// frame is the interpreter's as for any call, so what calls a function (a method of the library, the
// engine, another function) does not know which kind it is.
#pragma once

#include "runtime.h"

#include <cstdint>

namespace kas3 {
namespace vm {

struct NativeCode {
	virtual ~NativeCode() {}
	// Carries out a Native instruction of the function (by its number in the module): `p_what` and
	// `p_register` are the instruction's second and third operands, which only the native side reads.
	// p_registers are the frame's, valid until the native side calls anything; p_given is how many
	// arguments the frame was given. What it gives for the instruction's first operand goes to *r_result,
	// with a reference of its own (null when the instruction has none). False if an error is on its way
	// out of native code: the interpreter then raises an error of its own in its place, which leaves its
	// frames as any error does.
	[[nodiscard]] virtual bool run_native(uint32_t p_function, uint32_t p_what, uint32_t p_register, Slot *p_registers, uint32_t p_given,
			Slot *r_result) = 0;
};

} // namespace vm
} // namespace kas3
