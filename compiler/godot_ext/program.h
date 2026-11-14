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
	// A number that changes every time the scripts are loaded afresh. What belongs to an earlier load
	// (a script object held by a callable) must not be used with a later one.
	static uint64_t generation();

	bool valid() const { return valid_; }
	vm::Interpreter &interpreter() { return *interpreter_; }
	GodotHost &host() { return host_; }
	const ir::Module &module() const { return module_; }
	// The class a script file declares: the one named after the file. NONE if there is none.
	uint32_t class_of_file(const std::string &p_path) const;
};

// AS3 runs on one thread, the one the extension was started on. Godot can call a script from another (a
// task given to its worker threads): such a call is refused, and said so, before it touches anything.
void note_main_thread();
bool on_main_thread();
// Reports the refusal: the function Godot tried to call.
void refuse_thread(uint32_t p_function);

// Godot calling a function of a script object, from a method call or through a bound method: the arguments
// in as the parameters expect them (refused, never bent to fit), the result out. An AS3 error ends here:
// the interpreter has reported it, and the engine carries on.
void call_script(vm::Object *p_object, uint32_t p_function, const godot::Variant *const *p_arguments, int64_t p_count,
		godot::Variant *r_return, GDExtensionCallError *r_error);

} // namespace kas3
