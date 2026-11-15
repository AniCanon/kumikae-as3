// A method of a script object as a function Godot can call: what a script hands over when it writes
// `timer.timeout.add(onTimeout)`.
#pragma once

#include "engine_types.h"
#include "reload.h"
#include "runtime.h"

#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <vector>

namespace kas3 {

// A new Callable for the method, as a Variant on the heap. The script object is kept alive for as long as
// the callable lives. p_owner is the Godot object the script object is attached to, or null: Godot drops
// the callable's connections when that object is freed, and a callable kept elsewhere then refuses to run.
godot::Variant *make_callable(vm::Object *p_object, uint32_t p_function, godot::Object *p_owner);

// Tells a callable made here that it has been added to that signal as a listener.
void listener_added(const godot::Callable &p_callable, const godot::Signal &p_signal);
// For the cycle collector (GodotHost::node_edges, let_go). The listeners an object's signals have now that
// the collector looks through, one for each connection; what such a listener holds; and taking listeners
// out of their signals, to see whether anything else holds them, which puts everything back, in order, and
// returns those that outlived it.
void listeners_of(godot::Object *p_object, std::vector<uint64_t> *r_nodes);
bool listener_edges(uint64_t p_node, vm::EngineNode *r_node);
std::vector<uint64_t> let_go_listeners(const std::vector<uint64_t> &p_nodes);
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
