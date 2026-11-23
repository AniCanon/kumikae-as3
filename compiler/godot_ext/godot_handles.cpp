#include "godot_calls.h"

#include "any.h"
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

// The Variant after the head must retain the allocator's alignment for boxing into an untyped value.
static_assert(sizeof(rt::HandleHead) % vm::any::POINTER_ALIGNMENT == 0 &&
		__STDCPP_DEFAULT_NEW_ALIGNMENT__ >= vm::any::POINTER_ALIGNMENT);

void *handle_memory() {
	std::vector<void *> &spare = spare_handles();
	void *memory = nullptr;
	if (!on_main_thread() || spare.empty()) {
		memory = ::operator new(sizeof(rt::HandleHead) + sizeof(Variant));
	} else {
		memory = spare.back();
		spare.pop_back();
	}
	rt::HandleHead *head = new (memory) rt::HandleHead();
	head->holders.store(1, std::memory_order_relaxed);
	head->shared = rt::HANDLE_UNKNOWN;
	head->asked = 0;
	head->seen = 0;
	head->object = nullptr;
	return head + 1;
}

void free_handle_memory(void *p_memory) {
	void *memory = rt::head_of(p_memory);
	std::vector<void *> &spare = spare_handles();
	if (!on_main_thread() || spare.size() >= SPARE_HANDLES) {
		::operator delete(memory);
		return;
	}
	spare.push_back(memory);
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
		return value_name(value->get_type()) == p_class;
	}
	if (!value || is_freed(*value)) {
		return false;
	}
	const Object *object = *value;
	return object && object->is_class(text_of(p_class));
}

vm::Str *GodotHost::text(void *p_value) {
	script_effect();
	// A freed object is null to a script, in words too (Godot's text would be `<Freed Object>`). So is an
	// empty handle, which a copy of one on a freed counted object is.
	if (!present(p_value)) {
		return vm::new_string(NULL_TEXT);
	}
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
	rt::head_of(memory)->shared = rt::HANDLE_BORROWED;
	return memory;
}

// A handle on an object that is not reference-counted is shared by whoever copies it: one more holds it,
// and no other handle is made (generated code counts itself in the same way: rt::shared). Such a handle
// counts no reference of Godot's, is never written in place (only a value's is: assign(), Wanted::reuse)
// and is no node of the cycle collector's, so one handle serves every holder; a freed object is as null
// through it as through a copy. Not one on a reference-counted object, where each handle is a reference
// the collector counts, nor a script's hold on what it is attached to, whose copies are ordinary handles.
// Asked of a handle on an object only: a value lent for a call (script_call.cpp) has no head to look at.
bool GodotHost::shares(void *p_object_handle) {
	rt::HandleHead *head = rt::head_of(p_object_handle);
	if (head->shared == rt::HANDLE_UNKNOWN) {
		const ObjectID id = (ObjectID) * static_cast<const Variant *>(p_object_handle);
		head->shared = !id.is_null() && !id.is_ref_counted() ? rt::HANDLE_SHARED : rt::HANDLE_COPIED;
	}
	return head->shared == rt::HANDLE_SHARED;
}

void *GodotHost::copy(void *p_value) {
	if (!p_value) {
		return nullptr;
	}
	Variant *value = static_cast<Variant *>(p_value);
	if (value->get_type() == Variant::OBJECT && shares(p_value)) {
		rt::head_of(p_value)->holders.fetch_add(1, std::memory_order_relaxed);
		return p_value;
	}
	if (is_dangling(*value)) {
		return make_handle();
	}
	// Copying an engine reference can invoke a ScriptInstance reference-count callback.
	if (value->get_type() == Variant::OBJECT) {
		script_effect();
	}
	return make_handle(*value);
}

bool GodotHost::assign(void *p_to, const void *p_from) {
	script_effect();
	// An object (a value held as any of Godot's) is copied as copy() copies one: a freed one goes as null.
	// Nor is a handle on an object ever written over: others may hold the same one (shares()).
	const Variant *from = static_cast<const Variant *>(p_from);
	if (from->get_type() == Variant::OBJECT || static_cast<const Variant *>(p_to)->get_type() == Variant::OBJECT) {
		return false;
	}
	*static_cast<Variant *>(p_to) = *from;
	return true;
}

bool GodotHost::assign_math(void *p_to, const void *p_from, std::string_view p_class) {
	auto &to = *static_cast<Variant *>(p_to);
	const auto &from = *static_cast<const Variant *>(p_from);
	const auto type = from.get_type();
	if (type < Variant::VECTOR2 || type > Variant::COLOR || type != to.get_type() || (!p_class.empty() && value_name(type) != p_class)) {
		return false;
	}
	to = from;
	return true;
}

void GodotHost::drop(void *p_value) {
	if (!p_value) {
		return;
	}
	Variant *value = static_cast<Variant *>(p_value);
	// Only a handle on an object can be a borrowed one: a value (a Vector2) is freed without looking.
	if (value->get_type() == Variant::OBJECT) {
		// A shared one goes when the last of those that hold it lets go.
		rt::HandleHead *head = rt::head_of(p_value);
		if (head->shared == rt::HANDLE_SHARED && head->holders.fetch_sub(1, std::memory_order_acq_rel) > 1) {
			return;
		}
		if (head->shared == rt::HANDLE_BORROWED) {
			free_handle_memory(p_value);
			return;
		}
	}
	script_effect(); // Releasing a Variant can run a foreign destructor.
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
	if (!counted || (rt::head_of(p_value)->shared == rt::HANDLE_BORROWED && !joined_to_script(counted))) {
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
	script_effect();
	return let_go_listeners(p_nodes);
}

} // namespace kas3
