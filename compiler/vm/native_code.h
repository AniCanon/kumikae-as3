// A module whose functions have their bodies as native code (the C++ backend's): each such function's code
// is one Native instruction, and the interpreter asks whoever holds the native code to carry it out. The
// frame is the interpreter's as for any call, so what calls a function (a method of the library, the
// engine, another function) does not know which kind it is.
#pragma once

#include "runtime.h"

#include <cstdint>
#include <string>

namespace kas3 {
namespace vm {

// An error of native code's own, handed to the interpreter: what was thrown (with its reference), the trace
// native code built as its functions were left, and where it was thrown.
struct NativeError {
	Slot error;
	std::string trace;
	size_t lines = 0;
	std::string function;
	std::string file;
	int32_t line = 0;
};

struct NativeCode {
	virtual ~NativeCode() {}
	// After run_native() gave false: the error that is on its way out of native code, if the interpreter
	// is to carry it on (it called the function for the engine, or for anything but native code itself):
	// true, and the error is the interpreter's. False if the native side keeps it, having begun what led
	// to the call: the interpreter then raises a placeholder that only leaves its own frames.
	[[nodiscard]] virtual bool hand_over(NativeError *r_error) = 0;
	// The static fields and package variables native code keeps are emptied: Interpreter::clear_globals().
	virtual void clear_globals() = 0;
	// Carries out a Native instruction of the function (by its number in the module): `p_what` and
	// `p_register` are the instruction's second and third operands, which only the native side reads.
	// p_registers are the frame's, valid until the native side calls anything; p_given is how many
	// arguments the frame was given; what it was given beyond its parameters (a function that names
	// `arguments`, ir::Function::takes_extra) follows the registers, untyped, p_extras of them. What it gives for the instruction's first operand goes to *r_result,
	// with a reference of its own (null when the instruction has none). False if an error is on its way
	// out of native code (hand_over).
	[[nodiscard]] virtual bool run_native(uint32_t p_function, uint32_t p_what, uint32_t p_register, Slot *p_registers, uint32_t p_given,
			uint32_t p_extras, Slot *r_result) = 0;
};

} // namespace vm
} // namespace kas3
