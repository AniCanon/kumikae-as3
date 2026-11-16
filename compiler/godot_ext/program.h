// The project's AS3, compiled and ready to run: one program for the whole project.
#pragma once

#include "godot_host.h"
#include "interpreter.h"
#include "ir.h"
#include "reload.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace godot {
class Node;
class RefCounted;
}

namespace kas3 {

class Project {
	// On the heap: the interpreter refers to it, and a reload puts another in its place.
	std::unique_ptr<ir::Module> module_ = std::make_unique<ir::Module>();
	GodotHost host_;
	std::unique_ptr<vm::Interpreter> interpreter_;
	bool loaded_ = false;
	bool valid_ = false;

	Project() {}

public:
	// The one program, compiled from every .as file under the project folder the first time it is asked
	// for. Errors are printed to Godot's output; the program is then not valid.
	static Project &get();
	// Godot is closing (its main loop, whatever kind, has finished; other scripting languages are still
	// there): what the program holds beyond the scene (in static fields, and through listeners) is let go
	// now. Later, when this library is unloaded, a Godot object that a listener of another language is
	// attached to can no longer be freed safely. Until now, AS3 run as the scene tree goes works as ever.
	static void before_shutdown();
	// Forgets the program: the next get() compiles again. Every script instance must be gone.
	static void unload();
	// A number that changes every time the scripts are loaded afresh. What belongs to an earlier load
	// (a script object held by a callable) must not be used with a later one.
	static uint64_t generation();

	bool valid() const { return valid_; }
	vm::Interpreter &interpreter() { return *interpreter_; }
	GodotHost &host() { return host_; }
	const ir::Module &module() const { return *module_; }
	// The scripts were edited while the game runs: they are compiled again and, if they compile, every
	// script object alive is moved onto the new code with what it holds. With errors, the errors are
	// reported and the game carries on with the scripts it had. Not to be called while a script runs.
	static void reload();
	// The same, asked for now (the language's reload_scripts(), Godot's debugger): refused while AS3 runs.
	static void reload_now();
	// Once a frame while the project is a folder a person can edit: looks for changed scripts.
	static void watch();
	// Once a frame: a step of the cycle collector, if the program is loaded.
	static void collect_step();
	// The class a script file declares: the one named after the file. NONE if there is none.
	uint32_t class_of_file(std::string_view p_path) const;
};

// For a reload: whether every script instance's object has a place in the new program, and the move.
bool instances_can_move(vm::Reload &p_reload);
void move_instances(vm::Interpreter &p_old, vm::Reload &p_reload);
// The script objects of the Godot objects that a scene started again does not make again: those that are
// neither the scene nor in it (an autoload, a resource), with each one's Godot object. All, with no scene.
std::vector<std::pair<vm::Object *, godot::Object *>> instances_outside(const godot::Node *p_scene);

// A scene started again after a reload (restart.cpp): code that runs once changed in `classes`, of which
// objects are alive. What a restart would not make again is said: `kept` names it; `statics` names the
// classes whose static fields' first values changed after the fields were given them; `scenes` the scene
// files that changed (refresh_scenes): the running scene starts again if its own did.
struct Restart {
	std::vector<std::string> classes;
	std::vector<std::string> kept;
	std::vector<std::string> statics;
	std::vector<std::string> scenes;
};
// Starts the running scene again, from its file, deferred to when Godot changes scenes; or says why not.
// While a scene is being started again (from the deferred call to Godot saying the scene changed), a reload
// starts it once more when it is ready, so that its objects have one build's code throughout.
void restart_scene(const Restart &p_restart);
// The script object of the AS3 script attached to a Godot object, or null: the script instance holds a
// reference to it, unless its Godot object is reference-counted (counted.cpp).
vm::Object *script_object(const godot::Object *p_owner);
// For the cycle collector (counted.cpp): whether a Godot object has a script object of a counted class;
// what the collector sees of a reference-counted one: its count, and its script object, if it has one.
bool joined_to_script(const godot::Object *p_owner);
void script_edges(godot::RefCounted *p_counted, vm::EngineNode *r_node);

// AS3 runs on one thread, the one the extension was started on. Godot can call a script from another (a
// task given to its worker threads): such a call is refused, and said so, before it touches anything. A
// script attached there is refused too (AS3Script::_instance_create).
void note_main_thread();
bool on_main_thread();
// Reports the refusal: the function Godot tried to call.
void refuse_thread(uint32_t p_function);
// What Godot frees on another thread (a script instance, when an object's last reference goes in a worker's
// task) is freed on the main thread: once a frame, before the instances are looked through (a reload), and
// as Godot shuts down or the program is unloaded.
void free_instances_freed_elsewhere();

// Godot calling a function of a script object, from a method call or through a bound method: the arguments
// in as the parameters expect them (refused, never bent to fit), the result out. An AS3 error ends here:
// the interpreter has reported it, and the engine carries on.
void call_script(vm::Object *p_object, uint32_t p_function, const godot::Variant *const *p_arguments, int64_t p_count,
		godot::Variant *r_return, GDExtensionCallError *r_error);

} // namespace kas3
