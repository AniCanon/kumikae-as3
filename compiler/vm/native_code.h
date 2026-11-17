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

// An error the library left on its way out, called by a host with none of the interpreter's code running
// to carry it: taken over by the host (Interpreter::take_error()). `stopped`: not even the error could be
// made, and what there was to say has been reported.
struct LeftError {
	bool error = false;
	bool stopped = false;
	Slot value{};
};

// What such a host asks of the interpreter besides (vm::Interpreter):
//
//   engine_instruction()  One instruction that asks the engine, carried out on registers the host holds,
//       with no frame: the first instruction of a function of one instruction. What it gives is put into
//       its register there; an error it raises is the interpreter's (left_error()). The host says that it
//       is calling (direct_began()), so that an error of a script the engine runs meanwhile comes back to
//       this call.
//   idle()  For a host that runs a function of the program itself when the engine calls one, with no frame
//       of the interpreter's: whether nothing is in progress or held that such a call would have to answer
//       to (a frame, an error on its way out or carried, a call of the library by the host beyond the
//       p_direct it answers for itself).
//   entered(), left()  Such a call is counted with the interpreter's own calls from outside, and starts
//       the same clock.
//   report_native()  An error of it that nothing takes is reported as one of the interpreter's own is.
struct NativeCode {
	virtual ~NativeCode() {}
	// After run_native() gave false: the error that is on its way out of native code, if the interpreter
	// is to carry it on (it called the function for the engine, or for anything but native code itself):
	// true, and the error is the interpreter's. False if the native side keeps it, having begun what led
	// to the call: the interpreter then raises a placeholder that only leaves its own frames.
	[[nodiscard]] virtual bool hand_over(NativeError *r_error) = 0;
	// The static fields and package variables native code keeps are emptied: Interpreter::clear_globals().
	virtual void clear_globals() = 0;
	// Where native code keeps a static field or package variable (by its number in the module): what the
	// interpreter reads and writes for one reached by name (`c.count`, a class held as a value).
	virtual Slot *global(uint32_t p_index) = 0;
	// Whether native code has given a class's static fields their values, or the library has for it: what a
	// reload onto another build asks, to give them to no class twice (vm/reload.h).
	virtual bool statics_given(uint32_t p_class) const = 0;
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
