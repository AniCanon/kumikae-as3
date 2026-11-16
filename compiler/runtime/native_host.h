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
#include "library.h"
#include "no_engine.h"
#include "operations.h"
#include "program.h"

#include <memory>
#include <string>
#include <vector>

namespace kas3 {
namespace rt {

// The operations the host itself asks of the library, each a thunk after the program's own.
enum class Builtin : uint32_t {
	NullCheck, ObjectText, NewEntry, IsInt, IsUint, IsNumber, IsString, IsBoolean, NumberToAny, WideToAny, NewArray,
	Apply, // A function value called with an Array of untyped arguments, as apply() calls it.
	AnyText, // String(x) of an untyped value.
	SplitByPattern, // split() by a RegExp.
};

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
	// Per class: its objects are fields and nothing more (no vector, no table, no Godot object made with
	// one), so new_object() can make one, on the collector's list where the collector looks at the class.
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
	// The error of a function the engine ran while generated code was calling the engine on bare values,
	// with no instruction of the interpreter's between: kept for that call, which asks (call_failed), and
	// reported where the engine's call of generated code ends if nothing asked.
	struct Held {
		bool active = false;
		uint32_t depth = 0; // The generated functions in progress when it was kept.
		Waiting error{};
	};
	Held held;
	// What `direct` is inside the innermost instruction generated code is asking the engine for with no
	// frame (engine_thunk), or NONE: a function the engine calls back then has only generated code and
	// that request beneath it, which takes its error as a call on bare values does.
	uint32_t asking = NONE;
	// The engine calling a function of the program (a script's method, a listener): the generated function
	// itself, with no interpreter between, where nothing of the interpreter's is in progress; its own call
	// otherwise. Either way an error is reported or carried as the interpreter does it (vm::Interpreter::call),
	// and one already on its way out is put back afterwards. The arguments are in the parameters' kinds, the
	// caller's; a result is the caller's to release. False if the function failed.
	[[nodiscard]] bool enter(uint32_t p_function, const Slot *p_arguments, uint32_t p_count, Slot *r_result);
	std::vector<KeptTrace> kept;
	Slot *updated = nullptr; // Where the thunk in progress hands back a register it moved on.
	bool stopped = false; // The library could not go on (no memory even for an error): it has said why.
	// The interpreter holds the error that is on its way out, until the instruction beneath ends: see thunk().
	bool carried = false;
	uint32_t nesting = 0; // Thunks in progress, one inside another.
	// With KAS3_NATIVE_COUNTS set, how many times each thunk ran, and the program's uses of the engine
	// that the host could not work out (each then goes by its thunk): said on standard error as the host
	// goes, for the tests that hold generated code to its fast paths. Empty, and nothing said, otherwise.
	std::vector<uint64_t> thunk_counts;
	std::vector<uint32_t> unresolved;
	void say_counts() const;
	// Calls of the library's functions of plain values in progress (runtime/direct.cpp): like a thunk,
	// generated code began what such a call leads to, and an error of a function called beneath it stays
	// the context's.
	uint32_t direct = 0;

	// What the library's functions of plain values (vm/library.h) ask of whoever runs the program, answered
	// for generated code (runtime/direct.cpp); and the library as those functions are handed it.
	std::unique_ptr<vm::Runner> asked;
	vm::Library library() { return vm::Library{ interpreter->heap(), *asked, interpreter->unmoved() }; }

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
		return thunk(&context, program.thunk_count + static_cast<uint32_t>(p_which), p_given, nullptr);
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

// After a call the host made of the library or of the engine with none of the interpreter's code running
// (Interpreter::direct_began()): an error the interpreter was left with, its own or one carried back from
// a script the engine ran meanwhile (carries() moved on from p_carries), is the context's from here.
void settle_direct(Context *c, uint32_t p_carries);
// An instruction that asks the engine, carried out for generated code on the values it hands over, with no
// frame of the interpreter's (runtime/engine_thunk.cpp): what thunk() does for those.
bool asks_engine(uint8_t p_op); // Whether the instruction is one of those.
Slot engine_thunk(Context *c, uint32_t p_function, const ThunkInfo &p_info, const Slot *p_given, Slot *r_updated);
// The runner for a context's generated code (runtime/direct.cpp).
std::unique_ptr<vm::Runner> runner_for(Context *c);

// Runs the program as `kas3c run` runs it under the interpreter: its traces on standard output, an error
// nothing catches on standard error, and the same exit status.
int run_standalone(const Program *p_program);

} // namespace rt
} // namespace kas3
