// A script object whose Godot object is reference-counted (a RefCounted, a Resource, or a RefCounted that
// carries a script whose base is Godot's Object): the two live as long as either is held from outside, by
// AS3 or by Godot, and no longer.
//
// The program's references count the script object; Godot's count the Godot object. The instance does not
// hold the script object, and the script object's own handle on its Godot object does not count. While
// the program holds the script object, the instance holds one reference to the Godot object. When the
// program lets go (EngineHandles::keep()), that reference is given up: if it was the last, the Godot
// object goes, and the script object with it; if not, the script object is kept until Godot frees the
// Godot object. Nothing here gives up a reference from inside Godot's own counting, where a last one can't
// be let go (Godot waits for itself).
//
// A script taken off its Godot object (set_script(null), or another script put in its place) ends the pair:
// the instance gives its reference back, and the script object, if the program still holds it, lives on
// alone, as a script object whose Node was freed does. Its fields are AS3's; its handle on the Godot object
// (field 0) is borrowed and keeps nothing, so the Godot object lives while Godot holds it, and once it is
// gone a call on it is AS3's error about a freed object.
#include "callable.h"
#include "instance.h"
#include "program.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>

#include <unordered_map>
#include <vector>

using namespace godot;

namespace kas3 {

namespace {

std::unordered_map<const vm::Object *, ScriptInstance *> &held() {
	static std::unordered_map<const vm::Object *, ScriptInstance *> all;
	return all;
}

// Godot objects whose last reference a script gave back as it was taken off them: Godot is inside
// set_script() then, and goes on using the object, so it is freed later (free_detached()), by its id, in
// case Godot freed it first. Its count is zero, and nothing can take a reference to it again.
std::vector<ObjectID> &detached() {
	static std::vector<ObjectID> all;
	return all;
}

} // namespace

bool counted_on(uint32_t p_class, const Object *p_owner) {
	return Project::get().module().classes[p_class].counted && Object::cast_to<RefCounted>(p_owner);
}

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

bool joined_to_script(const Object *p_owner) {
	const vm::Object *object = script_object(p_owner);
	return object && held().count(object);
}

// To the cycle collector the two are one. The script object's own handle on its Godot object counts as a
// reference (GodotHost::node), and the instance's does not: it stands for the program's references, which
// the collector counts on the script object itself. The script object lives as long as the Godot object,
// which holds no reference to it: it is joined, never taken off. Every script on a RefCounted is counted
// (counted_on()), whatever its class, so the instance never holds its script object here.
void script_edges(RefCounted *p_counted, vm::EngineNode *r_node) {
	r_node->references = (uint64_t)p_counted->get_reference_count();
	vm::Object *object = script_object(p_counted);
	const auto found = object ? held().find(object) : held().end();
	if (found != held().end()) {
		r_node->references += found->second->holding ? 0 : 1;
		r_node->joined.push_back(object);
	}
}

void counted_freed(ScriptInstance *p_instance) {
	vm::Object *object = p_instance->object;
	if (!object) {
		return; // The program let go of it, which is what freed the Godot object.
	}
	if (p_instance->holding) {
		// Only a script taken off its object can be freed while it holds it: Godot frees a held object only
		// when made to (memdelete), and then its id is gone before free_detached() looks.
		p_instance->holding = false;
		RefCounted *counted = Object::cast_to<RefCounted>(ObjectDB::get_instance(p_instance->owner_id));
		if (counted && counted->unreference()) {
			detached().push_back(ObjectID(counted->get_instance_id()));
		}
	}
	held().erase(object);
	forget_listeners(object);
	p_instance->object = nullptr;
	// The program holds none of it without holding the Godot object too: it has no reference left.
	if (object->references == 0) {
		Project::get().interpreter().free_kept(object);
	}
}

void free_detached() {
	for (const ObjectID id : detached()) {
		if (Object *gone = ObjectDB::get_instance(id)) {
			memdelete(gone);
		}
	}
	detached().clear();
}

void counted_moved(const vm::Object *p_old, ScriptInstance *p_instance) {
	held().erase(p_old);
	held()[p_instance->object] = p_instance;
}

} // namespace kas3
