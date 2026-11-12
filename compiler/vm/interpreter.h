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
#include <vector>

namespace kas3 {
namespace vm {

class Interpreter {
public:
	// Calls may nest this deep. One more is an AS3 error.
	static const size_t MAX_FRAMES = 10000;
	// A stack trace shows this many frames, then how many more there were.
	static const size_t MAX_TRACE = 32;

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
	StatementHook hook_ = nullptr;
	void *hook_data_ = nullptr;

	void drop(ir::Kind p_kind, Slot &p_slot);
	void drop_object(Object *p_object);
	void assign(ir::Kind p_kind, Slot &p_slot, void *p_value); // Takes over one reference.
	bool push(uint32_t p_function, const Slot *p_arguments, size_t p_count, uint32_t p_result);
	void pop();
	void raise(uint32_t p_class, const std::string &p_message);
	void unwind(size_t p_depth);
	bool run(size_t p_depth);
	bool call(uint32_t p_function, const Slot *p_arguments, size_t p_count);
	std::string site(const Frame &p_frame) const;
	std::string object_text(Object *p_object) const;
	bool is_class(const Object *p_object, uint32_t p_class) const;
	Str *text(const std::string &p_text);

public:
	Interpreter(const ir::Module &p_module, Host &p_host);
	~Interpreter();
	Interpreter(const Interpreter &) = delete;
	Interpreter &operator=(const Interpreter &) = delete;

	void set_statement_hook(StatementHook p_hook, void *p_data) {
		hook_ = p_hook;
		hook_data_ = p_data;
	}
	// Runs the static initializers, then constructs the entry class. False if an error reached the top:
	// the host has been given it.
	bool run_program();

	// For a debugger: the frames, innermost last.
	size_t depth() const { return frames_.size(); }
	const ir::Function &function_at(size_t p_frame) const { return module_.functions[frames_[p_frame].function]; }
	// The line (from 1) the frame is at.
	int line_at(size_t p_frame) const;
};

} // namespace vm
} // namespace kas3
