#include "godot_calls.h"

#include "callable.h"
#include "program.h"
#include "handles.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>

#include <new>
#include <vector>

using namespace godot;

namespace kas3 {

// Freed handles' memory, for the next (handles.h). Never destroyed: a handle may be freed as the game shuts
// down, after static objects are gone.
static std::vector<void *> &spare_handles() {
	static std::vector<void *> *spare = new std::vector<void *>();
	return *spare;
}

static constexpr size_t SPARE_HANDLES = 4096; // The most kept; more are freed.

void *handle_memory() {
	std::vector<void *> &spare = spare_handles();
	if (!on_main_thread() || spare.empty()) {
		return ::operator new(sizeof(Variant));
	}
	void *memory = spare.back();
	spare.pop_back();
	return memory;
}

void free_handle_memory(void *p_memory) {
	std::vector<void *> &spare = spare_handles();
	if (!on_main_thread() || spare.size() >= SPARE_HANDLES) {
		::operator delete(p_memory);
		return;
	}
	spare.push_back(p_memory);
}

// A handle on a reference-counted object that is gone. Copying it would count a reference on freed
// memory. (Only a script's hold on its own owner can be one: any other handle keeps its object alive.) A
// handle on any other freed object is safe to copy, and Godot knows it as a freed object.
static bool is_dangling(const Variant &p_value) {
	return p_value.get_type() == Variant::OBJECT && ((ObjectID)p_value).is_ref_counted() && !p_value.get_validated_object();
}

bool GodotHost::present(void *p_value) {
	const Variant *value = static_cast<const Variant *>(p_value);
	return value && value->get_type() != Variant::NIL && !is_freed(*value);
}

bool GodotHost::is_class(void *p_value, std::string_view p_class) {
	const Variant *value = static_cast<const Variant *>(p_value);
	if (value && value->get_type() != Variant::OBJECT) {
		// One of Godot's value types: its name is the class.
		return Variant::get_type_name(value->get_type()) == text_of(p_class);
	}
	if (!value || is_freed(*value)) {
		return false;
	}
	const Object *object = *value;
	return object && object->is_class(text_of(p_class));
}

vm::Str *GodotHost::text(void *p_value) {
	CharString utf8 = static_cast<Variant *>(p_value)->stringify().utf8();
	return vm::new_string(std::string_view(utf8.get_data(), (size_t)utf8.length()));
}

void *GodotHost::borrow(Object *p_object) {
	// A Variant holding a reference-counted object counts as a reference to it. This one must not, so its
	// reference is given back at once, and drop() frees the memory without running the Variant's
	// destructor, which would give it back a second time. A Variant holding an object owns no other memory.
	void *memory = handle_memory();
	new (memory) Variant(p_object);
	if (RefCounted *counted = Object::cast_to<RefCounted>(p_object)) {
		counted->unreference();
	}
	borrowed_.insert(memory);
	return memory;
}

void *GodotHost::copy(void *p_value) {
	if (!p_value) {
		return nullptr;
	}
	Variant *value = static_cast<Variant *>(p_value);
	if (is_dangling(*value)) {
		return make_handle();
	}
	return make_handle(*value);
}

bool GodotHost::assign(void *p_to, const void *p_from) {
	// An object (a value held as any of Godot's) is copied as copy() copies one: a freed one goes as null.
	const Variant *from = static_cast<const Variant *>(p_from);
	if (from->get_type() == Variant::OBJECT) {
		return false;
	}
	*static_cast<Variant *>(p_to) = *from;
	return true;
}

void GodotHost::drop(void *p_value) {
	if (!p_value) {
		return;
	}
	Variant *value = static_cast<Variant *>(p_value);
	// Only a handle on an object can be a borrowed one: a value (a Vector2) is freed without looking.
	if (value->get_type() == Variant::OBJECT) {
		const auto borrowed = borrowed_.find(p_value);
		if (borrowed != borrowed_.end()) {
			borrowed_.erase(borrowed);
			free_handle_memory(p_value);
			return;
		}
	}
	free_handle(value);
}

// A handle that counts as a reference to a reference-counted object, which may lead back to a script
// object: through a script of AS3's on it, or a listener on one of its signals. A borrowed handle (a
// script's hold on what it is attached to) counts as one only to a counted script object (counted.cpp).
uint64_t GodotHost::node(void *p_value) {
	if (!p_value) {
		return 0;
	}
	const Variant *value = static_cast<const Variant *>(p_value);
	if (value->get_type() != Variant::OBJECT) {
		return 0;
	}
	RefCounted *counted = Object::cast_to<RefCounted>(value->get_validated_object());
	if (!counted || (borrowed_.count(p_value) && !joined_to_script(counted))) {
		return 0;
	}
	return counted->get_instance_id();
}

// An object's id has its top bit set when the object is reference-counted; a listener's number never has.
bool GodotHost::node_edges(uint64_t p_node, vm::EngineNode *r_node) {
	const ObjectID id(p_node);
	if (!id.is_ref_counted()) {
		return listener_edges(p_node, r_node);
	}
	RefCounted *counted = Object::cast_to<RefCounted>(ObjectDB::get_instance(id));
	if (!counted) {
		return false;
	}
	r_node->exact = true;
	script_edges(counted, r_node);
	listeners_of(counted, &r_node->nodes);
	return true;
}

// Only listeners have counts that are not exact: an object is freed by Godot when the handles on it go.
std::vector<uint64_t> GodotHost::let_go(const std::vector<uint64_t> &p_nodes) {
	return let_go_listeners(p_nodes);
}

} // namespace kas3
