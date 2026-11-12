// The project's AS3, compiled and ready to run: one program for the whole project.
#pragma once

#include "godot_host.h"
#include "interpreter.h"
#include "ir.h"

#include <memory>
#include <string>

namespace kas3 {

class Project {
	ir::Module module_;
	GodotHost host_;
	std::unique_ptr<vm::Interpreter> interpreter_;
	bool loaded_ = false;
	bool valid_ = false;

	Project() {}

public:
	// The one program, compiled from every .as file under the project folder the first time it is asked
	// for. Errors are printed to Godot's output; the program is then not valid.
	static Project &get();
	// Forgets the program: the next get() compiles again. Every script instance must be gone.
	static void unload();

	bool valid() const { return valid_; }
	vm::Interpreter &interpreter() { return *interpreter_; }
	const ir::Module &module() const { return module_; }
	// The class a script file declares: the one named after the file. NONE if there is none.
	uint32_t class_of_file(const std::string &p_path) const;
};

} // namespace kas3
