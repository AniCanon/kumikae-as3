// A method of a script object as a function Godot can call: what a script hands over when it writes
// `timer.timeout.add(onTimeout)`.
#pragma once

#include "reload.h"
#include "runtime.h"

#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace kas3 {

// A new Callable for the method, as a Variant on the heap. The script object is kept alive for as long as
// the callable lives. p_owner is the Godot object the script object is attached to, or null: Godot drops
// the callable's connections when that object is freed, and a callable kept elsewhere then refuses to run.
godot::Variant *make_callable(vm::Object *p_object, uint32_t p_function, godot::Object *p_owner);

// Tells a callable made here that it has been added to that signal as a listener.
void listener_added(const godot::Callable &p_callable, const godot::Signal &p_signal);
// Once a frame: frees a listener and the object it listens to when they are held only by each other.
void sweep_listeners();
// Before the program is unloaded: every callable lets go of its script object, and refuses to run after.
void release_listeners();
// For a reload of the scripts. First, whether every callable's script object has a place in the new
// program; then, each callable is given its object's counterpart and its method's new place. One whose
// method is gone refuses to run from then on.
bool listeners_can_move(vm::Reload &p_reload);
void move_listeners(vm::Interpreter &p_old, vm::Reload &p_reload, uint64_t p_generation);
// Whether the callable is one of those: a listener that a script added.
bool is_script_listener(const godot::Callable &p_callable);

} // namespace kas3
