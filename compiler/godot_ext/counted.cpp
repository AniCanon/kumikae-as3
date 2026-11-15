// A script object whose Godot object is reference-counted (a RefCounted, a Resource): the two live as long
// as either is held from outside, by AS3 or by Godot, and no longer.
//
// The program's references count the script object; Godot's count the Godot object. The instance does not
// hold the script object, and the script object's own handle on its Godot object does not count. While
// the program holds the script object, the instance holds one reference to the Godot object. When the
// program lets go (EngineHandles::keep()), that reference is given up: if it was the last, the Godot
// object goes, and the script object with it; if not, the script object is kept until Godot frees the
// Godot object. Nothing here gives up a reference from inside Godot's own counting, where a last one can't
// be let go (Godot waits for itself).
#include "callable.h"
#include "instance.h"
#include "program.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/memory.hpp>

#include <unordered_map>

using namespace godot;

namespace kas3 {

namespace {

std::unordered_map<const vm::Object *, ScriptInstance *> &held() {
	static std::unordered_map<const vm::Object *, ScriptInstance *> all;
	return all;
}

} // namespace

void counted_attached(ScriptInstance *p_instance) {
	held()[p_instance->object] = p_instance;
	hold_if_referenced(p_instance->object);
}

void hold_if_referenced(vm::Object *p_object) {
	const auto found = held().find(p_object);
	if (found == held().end() || found->second->holding || p_object->references == 0) {
		return;
	}
	if (RefCounted *counted = Object::cast_to<RefCounted>(found->second->owner)) {
		found->second->holding = counted->reference();
	}
}

bool GodotHost::keep(vm::Object *p_object) {
	const auto found = held().find(p_object);
	if (found == held().end()) {
		return false; // Its Godot object is gone.
	}
	ScriptInstance *instance = found->second;
	if (!instance->holding) {
		return true; // Godot holds it.
	}
	instance->holding = false;
	RefCounted *counted = Object::cast_to<RefCounted>(instance->owner);
	if (!counted || !counted->unreference()) {
		return true; // Godot holds it still.
	}
	// That was the last reference: the Godot object goes now, and the script object with it, here.
	held().erase(found);
	instance->object = nullptr;
	forget_listeners(p_object);
	memdelete(counted);
	return false;
}

void counted_freed(ScriptInstance *p_instance) {
	vm::Object *object = p_instance->object;
	if (!object) {
		return; // The program let go of it, which is what freed the Godot object.
	}
	held().erase(object);
	forget_listeners(object);
	p_instance->object = nullptr;
	// The program holds none of it without holding the Godot object too: it has no reference left.
	if (object->references == 0) {
		Project::get().interpreter().free_kept(object);
	}
}

void counted_moved(const vm::Object *p_old, ScriptInstance *p_instance) {
	held().erase(p_old);
	held()[p_instance->object] = p_instance;
}

} // namespace kas3
