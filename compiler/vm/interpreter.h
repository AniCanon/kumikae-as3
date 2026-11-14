// Runs the IR.
//
// The interpreter keeps its own stack of frames and never recurses in C++ for an AS3 call. Deep recursion
// is therefore an AS3 error, a debugger can walk the frames, and a function could later be paused and
// resumed.
#pragma once

#include "host.h"
#include "ir.h"
#include "runtime.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kas3 {
namespace vm {

class Interpreter {
public:
	// Calls may nest this deep. One more is an AS3 error.
	static const size_t MAX_FRAMES = 10000;
	// A stack trace shows this many frames, then how many more there were.
	static const size_t MAX_TRACE = 32;
	// A call from outside may be made while another is in progress (the engine calls a script, which calls
	// the engine, which calls a script) this many levels deep.
	static const int MAX_ENTRIES = 64;

	// Called at each statement, when set: where a debugger stops. Returning false ends the run.
	typedef bool (*StatementHook)(Interpreter &p_interpreter, void *p_data);

private:
	struct Frame {
		uint32_t function;
		uint32_t pc;
		size_t base; // Where its registers start in stack_.
		uint32_t result; // The caller's register for the value returned, or NONE.
		uint32_t arguments; // How many the caller passed.
	};

	const ir::Module &module_;
	Host &host_;
	std::vector<Slot> stack_;
	std::vector<Frame> frames_;
	std::vector<Slot> globals_;
	std::vector<Str *> strings_; // The module's string constants.
	Str *null_text_ = nullptr; // "null"
	Object *pending_ = nullptr; // An error on its way out.
	std::string trace_; // Its stack trace, built as frames are left.
	size_t trace_lines_ = 0;
	bool stopped_ = false; // Nothing more is to run.
	bool halted_ = false; // Because the statement hook said so, not because of an error.
	bool started_ = false; // The package-level initializers have run.
	std::vector<uint8_t> class_used_; // Per class: its static fields have been (or are being) given values.
	int entries_ = 0; // Calls from outside in progress.
	std::vector<std::unordered_map<std::string, uint32_t>> methods_; // Per class: a method's function by name.
	StatementHook hook_ = nullptr;
	void *hook_data_ = nullptr;

	void drop(ir::Kind p_kind, Slot &p_slot);
	void drop_object(Object *p_object);
	void assign(ir::Kind p_kind, Slot &p_slot, void *p_value); // Takes over one reference.
	Slot share(ir::Kind p_kind, Slot p_value); // The value with a reference of its own.
	void put(ir::Kind p_kind, Slot &p_target, Slot p_owned); // Stores it, giving up what was there.
	const char *host_op(const ir::Function &p_function, const ir::Instruction &p_instruction);
	bool push(uint32_t p_function, const Slot *p_arguments, size_t p_count, uint32_t p_result);
	void pop();
	void raise(uint32_t p_class, const std::string &p_message);
	void unwind(size_t p_depth);
	bool run(size_t p_depth, Slot *r_result);
	bool call(uint32_t p_function, const Slot *p_arguments, size_t p_count, Slot *r_result = nullptr);
	void report();
	void forget_class(uint32_t p_class); // Its static fields were not all set: it is not used yet, after all.
	void use_class(uint32_t p_class); // From running code: pushes what must run first.
	bool use_class_now(uint32_t p_class); // From outside: runs it.
	std::string site(const Frame &p_frame) const;
	std::string object_text(Object *p_object) const;
	bool is_class(const Object *p_object, uint32_t p_class) const;
	Str *text(const std::string &p_text);

public:
	Interpreter(const ir::Module &p_module, Host &p_host);
	~Interpreter();
	// Empties every static field and package variable: what they held is let go. For a host that is
	// shutting down and must let go of the engine's objects while the engine can still free them.
	void clear_globals();
	Interpreter(const Interpreter &) = delete;
	Interpreter &operator=(const Interpreter &) = delete;

	void set_statement_hook(StatementHook p_hook, void *p_data) {
		hook_ = p_hook;
		hook_data_ = p_data;
	}
	// Runs the static initializers, then constructs the entry class. False if an error reached the top:
	// the host has been given it.
	bool run_program();

	// ---- for a host that keeps the interpreter and calls into it ----
	//
	// Each of these is an entry from outside. An error that nothing catches ends that entry only: the host
	// is given it, and the interpreter is fit for the next call. An entry may be made while another is in
	// progress.

	// Runs the static initializers, the first time it is called.
	bool start();
	// A new object of the class, its constructor run with no arguments. One reference, the caller's: give
	// it back with release_object(). Null if the constructor failed.
	// For a script class, p_owner is a handle on the Godot object it is attached to. The handle is handed
	// over: the script object drops it when it is freed, and so does this call if it fails. The host
	// decides what such a handle is; it should not keep the Godot object alive.
	Object *construct(uint32_t p_class, void *p_owner = nullptr);
	// A field by its AS3 name: the value as it is held (no new reference) and its kind. False if there is
	// no such field.
	bool get_field(Object *p_object, std::string_view p_name, Slot *r_value, ir::Kind *r_kind = nullptr) const;
	const ir::Module &module() const { return module_; }
	// Calls the object's method of that name. The arguments are in the method's parameter kinds, and
	// parameters left out take their defaults. A reference returned in r_result is the caller's to release.
	// False if there is no such method, or it failed.
	bool call_method(Object *p_object, std::string_view p_name, const Slot *p_arguments, size_t p_count, Slot *r_result);
	// Calls a function the host has already found. The first argument of a method is its object.
	bool call_function(uint32_t p_function, const Slot *p_arguments, size_t p_count, Slot *r_result) {
		return p_function < module_.functions.size() && call(p_function, p_arguments, p_count, r_result);
	}
	// Gives up a value the host was holding for the interpreter: a result, or an argument it made.
	void discard(ir::Kind p_kind, Slot p_value) { drop(p_kind, p_value); }
	// The method's function, or NONE: for a host that looks a name up once and keeps the answer.
	uint32_t find_method(uint32_t p_class, std::string_view p_name) const;
	void release_object(Object *p_object);

	// For a debugger: the frames, innermost last.
	size_t depth() const { return frames_.size(); }
	const ir::Function &function_at(size_t p_frame) const { return module_.functions[frames_[p_frame].function]; }
	// The line (from 1) the frame is at.
	int line_at(size_t p_frame) const;
	// A local variable or parameter of the frame by name: its value as held (no new reference) and its
	// kind. False if the frame's function has no such local.
	bool local_at(size_t p_frame, std::string_view p_name, Slot *r_value, ir::Kind *r_kind) const;
};

} // namespace vm
} // namespace kas3
