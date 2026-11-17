// The project's scripts as native code: a program library the C++ backend's output was built into
// (`kas3c cpp --godot`), loaded beside the interpreter's library and run in its place. The runtime's host
// (runtime/native_host.h) keeps an interpreter over the same module with every function's body native, so that
// everything here that calls a script, reads a field or moves an object goes on as it does: only what
// runs when a function is called differs.
#pragma once

#include "godot_host.h"
#include "native_host.h"
#include "shared_library.h"

#include <godot_cpp/variant/string_name.hpp>

#include <memory>
#include <string>
#include <vector>

namespace kas3 {

class NativeProgram {
	SharedLibrary library_;
	std::unique_ptr<rt::Host> host_;
	std::vector<godot::StringName> names_; // The program's names, each built once; never moved once made.
	std::vector<void *> singletons_; // The handles kept for the singletons it uses, and for the constants.
	GodotHost &engine_;

	explicit NativeProgram(GodotHost &p_engine) :
			engine_(p_engine) {}
	void resolve(const rt::Program &p_program);
	// One use of the engine worked out; false if generated code is left to ask the library there.
	bool resolve_use(const ir::Function &p_thunk, const ir::Instruction &p_in, rt::Resolved *r_resolved);
	bool resolve_value(const GodotHost::Worked &p_worked, rt::Resolved *r_resolved);

public:
	// Where the project's program library is looked for: the file KAS3_PROGRAM_LIBRARY names, or
	// `bin/libkas3_program` beside the project's scripts. Empty if there is none.
	static std::string path();
	// The library at that path for this module: null, with why in words, if it can't be loaded or was
	// built from other sources than the module was compiled from.
	static std::unique_ptr<NativeProgram> load(const std::string &p_path, const ir::Module &p_module, GodotHost &p_engine, std::string *r_why);
	~NativeProgram();
	NativeProgram(const NativeProgram &) = delete;
	NativeProgram &operator=(const NativeProgram &) = delete;

	vm::Interpreter &interpreter() { return *host_->interpreter; }
	// Godot calling a function of the scripts: the generated function itself where nothing of the
	// interpreter's is in progress (rt::Host::enter).
	[[nodiscard]] bool call(uint32_t p_function, const vm::Slot *p_arguments, size_t p_count, vm::Slot *r_result) {
		return host_->enter(p_function, p_arguments, static_cast<uint32_t>(p_count), r_result);
	}
	// Whether a function of the scripts is in progress, generated or the interpreter's: the scripts are not
	// reloaded meanwhile.
	bool running() const { return host_->interpreter->in_call() || host_->interpreter->depth() > 0 || host_->context.depth > 0; }
	// The engine freed a script instance: generated code that kept an object it found looks again.
	void instance_freed() { host_->context.freed.fetch_add(1, std::memory_order_relaxed); }
};

} // namespace kas3
