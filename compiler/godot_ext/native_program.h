// The project's scripts as native code: a program library the C++ backend's output was built into
// (`kas3c cpp --godot`), loaded beside the interpreter's library and run in its place. The runtime's host
// (runtime/native_host.h) keeps an interpreter over the same module with every function's body native, so that
// everything here that calls a script, reads a field or moves an object goes on as it does: only what
// runs when a function is called differs.
#pragma once

#include "godot_host.h"
#include "native_host.h"

#include <godot_cpp/variant/string_name.hpp>

#include <memory>
#include <string>
#include <vector>

namespace kas3 {

class NativeProgram {
	void *library_ = nullptr;
	std::unique_ptr<rt::Host> host_;
	std::vector<godot::StringName> names_; // The program's names, each built once; never moved once made.
	std::vector<void *> singletons_; // The handles kept for the singletons it uses.
	GodotHost &engine_;

	explicit NativeProgram(GodotHost &p_engine) :
			engine_(p_engine) {}
	void resolve(const rt::Program &p_program);

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
};

} // namespace kas3
