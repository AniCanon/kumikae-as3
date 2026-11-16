// One object's script instance, as instance.cpp and counted.cpp share it.
#pragma once

#include "runtime.h"
#include "script.h"

#include <godot_cpp/core/object.hpp>

struct ScriptInstance {
	kas3::vm::Object *object;
	godot::Object *owner;
	godot::Ref<AS3Script> script; // Kept alive for as long as anything runs it.
	// On a reference-counted object (counted_on()) the instance does not hold its script object: the
	// program's references do. While the program has any, the instance holds one reference to the Godot
	// object, so that neither goes while the other is held from outside, and neither keeps the other alive
	// when nothing else holds them.
	bool counted = false;
	bool holding = false; // That reference to the Godot object.
};

namespace kas3 {

// Whether a script of that class on that object is counted: its class may be (ir::Class::counted, which a
// script whose base is Godot's Object is too) and the object is a RefCounted.
bool counted_on(uint32_t p_class, const godot::Object *p_owner);
// A counted instance is made: it is found by its script object from now on.
void counted_attached(ScriptInstance *p_instance);
// AS3 ran on an object (a method, its constructor): if the program now holds a counted one, its Godot
// object is held too.
void hold_if_referenced(vm::Object *p_object);
// Godot frees the object a counted instance is on: its script object goes too, now.
void counted_freed(ScriptInstance *p_instance);
// A reload gave the instance its object in the new program.
void counted_moved(const vm::Object *p_old, ScriptInstance *p_instance);

} // namespace kas3
