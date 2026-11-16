// The host of a program library where there is no engine: what `Context::host` points at. Only the
// runtime's own files include this; generated code sees the context and runtime/operations.h.
//
// The library the interpreter has (strings' and vectors' methods, RegExp, Date, JSON, properties by name,
// the untyped value) is reached through an interpreter of its own, over a module that holds the program's
// classes and, for each function, a body that calls the generated one (runtime/stub_module.cpp). vm/ is
// used as it is: what should be split out of it so that no interpreter is needed is listed in the README.
#pragma once

#include "engine.h"
#include "interpreter.h"
#include "no_engine.h"
#include "operations.h"
#include "program.h"

#include <memory>
#include <string>
#include <vector>

namespace kas3 {
namespace rt {

// The operations the host itself asks of the library, each a thunk after the program's own.
enum class Builtin : uint32_t { NullCheck, Overflow, ObjectText, NewEntry, IsInt, IsUint, IsNumber, IsString, IsBoolean, NumberToAny, WideToAny, NewArray };

// What a Native instruction asks of the host (its second operand): a function's body run, or one of the
// two things a thunk hands back.
enum class NativeWhat : uint32_t {
	Body, // The generated function, with the frame's arguments.
	Caught, // A thunk's catch: the error in the register named is the context's now.
	Updated, // A thunk hands back the register named, which its instruction moved on.
};

// Where a host runs: what it prints to, the engine it asks, its clock, and the program's module where the
// embedder has it (the Godot extension compiles the scripts itself; a program run on its own has only its
// table, and the module is made from that).
struct Embedder {
	vm::Output &output;
	vm::EngineHandles &handles;
	vm::EngineSource &source;
	vm::Clock &clock;
	const ir::Module *module = nullptr;
};

// The module the interpreter is given, made from the program's table: its classes, globals and strings by
// their own numbers, each function as a stub, then the program's thunks, then the host's (Builtin).
ir::Module stub_module(const Program &p_program);
// The same from the program's own module: every function's body replaced, the thunks added.
ir::Module native_module(const ir::Module &p_module, const Program &p_program);

// A trace kept for an error a finally caught, for when it is thrown again (as vm::Kept).
struct KeptTrace {
	uint32_t depth;
	uint32_t reg;
	std::string trace;
	size_t lines;
};

struct Host final : vm::NativeCode {
	const Program &program;
	ir::Module module;
	vm::Output &output;
	vm::EngineHandles &handles;
	std::unique_ptr<vm::Interpreter> interpreter;
	// One entry for each of the program's uses of the engine and each name it gives: filled by a host that
	// has an engine (set_engine), empty otherwise.
	std::vector<Resolved> resolved;
	std::vector<const void *> names;
	Context context;
	std::vector<Slot> globals;
	std::vector<Str *> strings;
	Str *null_text = nullptr;
	std::vector<uint8_t> class_used;
	// Per class: its objects are plain fields that can lead to no other object, so the cycle collector
	// does not look at them and new_object() can make one.
	std::vector<uint8_t> made_here;
	// The stack trace of the error on its way out, built as generated functions are left (as the
	// interpreter builds its own: vm/interpreter_errors.cpp).
	std::string trace;
	size_t trace_lines = 0;
	uint32_t thrown_function = NONE; // Where the error on its way out was thrown: the trace's first line.
	uint32_t thrown_line = 0;
	// An error on its way out, set aside while a function is called from outside all the same (Godot going
	// on to a signal's next listener after one failed), to be on its way again when that call is over.
	struct Waiting {
		Slot error;
		std::string trace;
		size_t lines;
		uint32_t thrown_function;
		uint32_t thrown_line;
	};
	std::vector<Waiting> waiting;
	// The error now on its way out is of such a call: the interpreter is handed it, and reports it there.
	bool later = false;
	void set_aside();
	void restore();
	std::vector<KeptTrace> kept;
	Slot *updated = nullptr; // Where the thunk in progress hands back a register it moved on.
	bool stopped = false; // The library could not go on (no memory even for an error): it has said why.
	// The interpreter holds the error that is on its way out, until the instruction beneath ends: see thunk().
	bool carried = false;
	uint32_t nesting = 0; // Thunks in progress, one inside another.

	Host(const Program &p_program, const Embedder &p_embedder);
	~Host() override;
	[[nodiscard]] bool run_native(uint32_t p_function, uint32_t p_what, uint32_t p_register, Slot *p_registers, uint32_t p_given,
			Slot *r_result) override;
	[[nodiscard]] bool hand_over(vm::NativeError *r_error) override;
	void clear_globals() override;
	Host(const Host &) = delete;
	Host &operator=(const Host &) = delete;

	uint32_t thunk_function(uint32_t p_thunk) const { return program.function_count + p_thunk; }
	Slot builtin(Builtin p_which, const Slot *p_given) {
		return thunk(&context, program.thunk_count + static_cast<uint32_t>(p_which), p_given);
	}
	// A class's first use (operations.h, use_class). From running code, initializers that had not begun
	// when one failed are in the error's trace, as the interpreter has them; from the host, they are not.
	void use_class(uint32_t p_class, bool p_from_code);
	// The library uses classes too (`new c()` of a class held as a value), and keeps its own account of
	// which are set up: when it runs a function that sets a class's static fields, they are set once,
	// whoever used the class first. True if the function was one, and has been dealt with.
	bool set_up_by_library(uint32_t p_function);
	// The package's variables are set, then the entry class is made. False if an error reached the top:
	// it has been reported.
	bool run();
	// An error's name and message, then the generated functions in progress, innermost first; without the
	// error's own constructors, for an error the program is making.
	std::string stack_text(Object *p_error, bool p_skip_constructors);
	// Gives the error on its way out to standard error, with its trace, and forgets it.
	void report();
};

// Runs the program as `kas3c run` runs it under the interpreter: its traces on standard output, an error
// nothing catches on standard error, and the same exit status.
int run_standalone(const Program *p_program);

} // namespace rt
} // namespace kas3
