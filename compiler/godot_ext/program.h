// The project's AS3, compiled and ready to run: one program for the whole project.
#pragma once

#include "godot_host.h"
#include "interpreter.h"
#include "native_program.h"
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
	// Shared pointer so worker threads inspecting metadata (e.g. threaded ResourceLoader or refuse_thread)
	// hold a safe snapshot across reloads without touching Project::get() off the main thread.
	std::shared_ptr<ir::Module> module_ = std::make_shared<ir::Module>();
	GodotHost host_;
	std::unique_ptr<vm::Interpreter> interpreter_;
	// The scripts as native code, where the project has a program library built from them as they are
	// now: its host's interpreter is then the one everything here asks, and interpreter_ is not made.
	std::unique_ptr<NativeProgram> native_;
	bool loaded_ = false;
	bool valid_ = false;
	std::string load_error_;
	static uint64_t loads_; // How many times the scripts have been loaded afresh (generation()).

	Project() {}

public:
	// The one program, compiled from every .as file under the project folder the first time it is asked
	// for. Errors are printed to Godot's output; the program is then not valid.
	static Project &get();
	// Thread-safe snapshot of the currently loaded module, or null if no valid program is loaded.
	static std::shared_ptr<const ir::Module> module_snapshot();
	// Godot is closing (its main loop, whatever kind, has finished; other scripting languages are still
	// there): what the program holds beyond the scene (in static fields, and through listeners) is let go
	// now. Later, when this library is unloaded, a Godot object that a listener of another language is
	// attached to can no longer be freed safely. Until now, AS3 run as the scene tree goes works as ever.
	static void before_shutdown();
	// Forgets the program: the next get() compiles again. Every script instance must be gone.
	static void unload();
	// A number that changes every time the scripts are loaded afresh. What belongs to an earlier load
	// (a script object held by a callable) must not be used with a later one.
	// Asked each time Godot asks whether a listener is still valid: nothing is called for it.
	static uint64_t generation() { return loads_; }

	bool valid() const { return valid_; }
	vm::Interpreter &interpreter() { return native_ ? native_->interpreter() : *interpreter_; }
	bool native() const { return native_ != nullptr; }
	// Whether a function of the scripts is in progress, by whichever runs them.
	bool running() { return native_ ? native_->running() : interpreter_->depth() > 0; }
	// Godot calling a function of the scripts, by whichever runs them: the first argument of a method is
	// its object, and a result is the caller's to give up. False if it failed: the error has been reported,
	// or is carried to the script's own call of the engine that led here.
	[[nodiscard]] bool call_function(uint32_t p_function, const vm::Slot *p_arguments, size_t p_count, vm::Slot *r_result) {
		host_.script_entry();
		return native_ ? native_->call(p_function, p_arguments, p_count, r_result)
					   : interpreter_->call_function(p_function, p_arguments, p_count, r_result);
	}
	bool native_caller(const vm::Object **r_object, uint32_t *r_class) const {
		return native_ && native_->caller(r_object, r_class);
	}
	GodotHost &host() { return host_; }
	const ir::Module &module() const { return *module_; }
	// The scripts were edited while the game runs: they are compiled again and, if they compile, every
	// script object alive is moved onto the new code with what it holds, and the objects whose constructors
	// would give them other values now are updated in place where that can be done (vm/reload.h); what was
	// and what was not is said in one line. With errors, the errors are reported and the game carries on
	// with the scripts it had. Not to be called while a script runs. A game that runs as native code is
	// moved the same way onto the interpreter, which runs the new code from then on, and says so: native
	// code is not reloaded in a running game, and the library of the new code is built for the next run.
	static void reload();
	// Same as reload(), returning false with diagnostics when scripts fail to compile or move.
	static bool reload_with_errors(std::string *r_errors);
	// The same, asked for now (the language's reload_scripts(), Godot's debugger): refused while AS3 runs.
	static void reload_now();
	// The running scene started again from its file, asked for by a person (the language's restart_scene()):
	// what shows the edits a reload could not show on the objects alive. A reload never does this by itself.
	static void restart_scene();
	// Once a frame while the project is a folder a person can edit: looks for changed scripts.
	static void watch();
	// How many looks the watch has made, each ended with what it began (a reload too): a test that saves
	// a file waits for looks, not for time. A save is acted on by the second look after it (see watch()).
	static uint64_t watch_looks();
	// Once a frame: a step of the cycle collector, if the program is loaded.
	static void collect_step();
	// The class a script file declares: the one named after the file. NONE if there is none.
	uint32_t class_of_file(std::string_view p_path) const;
	static uint32_t class_of_file(const ir::Module &p_module, std::string_view p_path);
};

// For a reload: whether every script instance's object has a place in the new program, and the move.
bool instances_can_move(vm::Reload &p_reload);
void move_instances(vm::Interpreter &p_old, vm::Reload &p_reload);
// The script objects of the Godot objects that a scene started again does not make again: those that are
// neither the scene nor in it (an autoload, a resource), with each one's Godot object. All, with no scene.
std::vector<std::pair<vm::Object *, godot::Object *>> instances_outside(const godot::Node *p_scene);

// What a reload did beyond moving the objects: which classes' objects alive were updated in place and which
// were left and why (vm::InPlace), and the classes whose static fields' first values changed after the
// fields were given them (`statics`).
struct Moved {
	vm::InPlace in_place;
	std::vector<std::string> statics;
};
// Says it (restart.cpp): one line for what was updated in place and what was not, one for the static fields.
void say_moved(const Moved &p_moved);

// The running scene started again from its file (restart.cpp). `asked`: a person asked for it; `classes`
// names those whose objects reloads since the scene started left as they were, and `kept` what of them a
// restart does not make again. Otherwise it is a reload's doing, which starts the scene again only if the
// scene's own file changed (`scenes`: the scene files that did, refresh_scenes).
struct Restart {
	bool asked = false;
	std::vector<std::string> classes;
	std::vector<std::string> kept;
	std::vector<std::string> scenes;
};
// Starts the running scene again, deferred to when Godot changes scenes; or says why not. While a scene is
// being started again (from the deferred call to Godot saying the scene changed), a reload starts it once
// more when it is ready, so that its objects have one build's code throughout.
void restart_scene(const Restart &p_restart);
// The script object of the AS3 script attached to a Godot object, or null: the script instance holds a
// reference to it, unless its Godot object is reference-counted (counted.cpp).
vm::Object *script_object(const godot::Object *p_owner);
vm::Object *script_on_live_handle(void *p_live, const godot::Variant &p_handle);
vm::Object *script_object_by_id(uint64_t p_id);
// What arrives where an object of the program's class p_class is declared and Godot gives p_value: the
// script object of the Godot object p_value holds, if that object carries a script whose class is p_class
// or extends it. Null for anything else: not an object, one freed, one with no script of the program's or
// with a script of another class. Every way a value of Godot's reaches a place of a script's class asks
// here (counted.cpp): a call's arguments and a field set from Godot (members.h, object_arrives), and the
// program's own `is`, `as` and stores of an untyped value (GodotHost::script_of).
vm::Object *arriving_script(const godot::Variant &p_value, uint32_t p_class);
// One that arrived, as the place it arrives in holds it: with a reference of its own, and, where its Godot
// object is reference-counted, that object held for as long as the program holds this one.
vm::Object *taken_script(vm::Object *p_object);
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
// The program is unloaded: caches holding Godot strings/names/signals go while Godot is still there to free them.
void forget_reached();
void forget_restart();
void forget_class_text();
void forget_class_signals();
void forget_folders();
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
