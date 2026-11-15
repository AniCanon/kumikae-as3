#include "interpreter_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

// Gives up the reference a slot holds.
void Interpreter::drop(Kind p_kind, Slot &p_slot) {
	if (p_kind == Kind::Str) {
		Str *string = static_cast<Str *>(p_slot.p);
		if (release(string)) {
			free_string(string);
		}
	} else if (p_kind == Kind::Obj) {
		Object *object = static_cast<Object *>(p_slot.p);
		if (release(object)) {
			drop_object(object);
		}
	} else if (ir::is_engine(p_kind) && p_slot.p) {
		// Letting go of an engine value can run engine code that calls back in and moves the registers:
		// the slot is emptied first and not touched again.
		void *handle = p_slot.p;
		p_slot.bits = 0;
		host_.handles.drop(handle);
		return;
	} else if (p_kind == Kind::Any) {
		const Slot value = p_slot;
		p_slot.bits = 0; // For the same reason.
		drop_any(value);
		return;
	}
	p_slot.bits = 0;
}

void Interpreter::drop_any(Slot p_value) {
	switch (any::tag(p_value)) {
		case AnyTag::String: {
			Str *string = static_cast<Str *>(any::pointer(p_value));
			if (release(string)) {
				free_string(string);
			}
			break;
		}
		case AnyTag::Object: {
			Object *object = static_cast<Object *>(any::pointer(p_value));
			if (release(object)) {
				drop_object(object);
			}
			break;
		}
		case AnyTag::Wide: {
			Wide *wide = static_cast<Wide *>(any::pointer(p_value));
			if (release(wide)) {
				free_wide(wide);
			}
			break;
		}
		case AnyTag::Value:
		case AnyTag::Godot:
			host_.handles.drop(any::pointer(p_value));
			break;
		default:
			break;
	}
}

Slot Interpreter::share(Kind p_kind, Slot p_value) {
	if (p_kind == Kind::Any) {
		retain(any::counted(p_value));
		if (any::tag(p_value) == AnyTag::Value) {
			return any::godot_value(host_.handles.copy(any::pointer(p_value)));
		}
		if (any::tag(p_value) == AnyTag::Godot) {
			return any::godot_object(host_.handles.copy(any::pointer(p_value)));
		}
		return p_value;
	}
	if (ir::is_reference(p_kind)) {
		retain(static_cast<Counted *>(p_value.p));
	} else if (ir::is_engine(p_kind) && p_value.p) {
		p_value.p = host_.handles.copy(p_value.p);
	}
	return p_value;
}

void Interpreter::put(Kind p_kind, Slot &p_target, Slot p_owned) {
	Slot old = p_target;
	p_target = p_owned;
	if (held(p_kind)) {
		drop(p_kind, old);
	}
}

// Frees an object whose last reference is gone, and whatever that in turn leaves unreferenced. A list
// rather than recursion: a long chain of objects must not overflow the C++ stack. Letting go of an engine
// value can run code that frees more objects: those join the list of the call already freeing, which keeps
// one list for every call and allocates nothing once it has grown.
void Interpreter::drop_object(Object *p_object) {
	dying_.push_back(p_object);
	if (freeing_) {
		return;
	}
	freeing_ = true;
	while (!dying_.empty()) {
		Object *object = dying_.back();
		dying_.pop_back();
		const ir::Class &klass = module_.classes[object->klass];
		each_held(klass, object, [this](Kind p_kind, Slot &p_slot) { let_go(p_kind, p_slot); });
		if (klass.element != Kind::Void) {
			free_vector(static_cast<Vector *>(object));
		} else {
			free_object(object);
		}
	}
	freeing_ = false;
}

// Empties one slot of an object being freed. An object it held whose last reference this was goes on the
// list of those to free, as drop_object() keeps it.
void Interpreter::let_go(Kind p_kind, Slot &p_slot) {
	const Slot value = p_slot;
	p_slot.bits = 0; // First: letting go of an engine value can run code that reads it.
	Object *other = p_kind == Kind::Obj ? static_cast<Object *>(value.p)
			: p_kind == Kind::Any && any::tag(value) == AnyTag::Object ? static_cast<Object *>(any::pointer(value))
																	 : nullptr;
	if (other) {
		if (release(other)) {
			dying_.push_back(other);
		}
	} else if (p_kind == Kind::Any) {
		drop_any(value);
	} else {
		Slot copy = value;
		drop(p_kind, copy);
	}
}

void Interpreter::assign(Kind p_kind, Slot &p_slot, void *p_value) {
	Slot old = p_slot;
	p_slot.bits = 0;
	p_slot.p = p_value;
	drop(p_kind, old);
}

// Null, and the error raised, when there is no memory for it.
Str *Interpreter::text(const std::string &p_text) {
	Str *made = new_string(p_text);
	if (!made) {
		out_of_memory();
	}
	return made;
}

bool Interpreter::is_class(const Object *p_object, uint32_t p_class) const {
	if (!p_object) {
		return false;
	}
	for (uint32_t at = p_object->klass; at != NONE; at = module_.classes[at].base) {
		if (at == p_class) {
			return true;
		}
	}
	return false;
}

std::string Interpreter::object_text(Object *p_object) const {
	if (!p_object) {
		return "null";
	}
	const std::string &name = module_.classes[p_object->klass].name;
	std::string simple = name.substr(name.rfind('.') == std::string::npos ? 0 : name.rfind('.') + 1);
	if (module_.error_class != NONE && is_class(p_object, module_.error_class)) {
		return error_heading(p_object); // A class of the program's that extends Error is named "Error".
	}
	return "[object " + simple + "]";
}

bool Interpreter::get_field(Object *p_object, std::string_view p_name, Slot *r_value, Kind *r_kind) const {
	if (!p_object) {
		return false;
	}
	const ir::Class &klass = module_.classes[p_object->klass];
	// The class's own field hides a base's of the same name: the last one wins.
	for (size_t i = klass.fields.size(); i-- > 0;) {
		if (klass.fields[i].name == p_name) {
			*r_value = p_object->fields()[i];
			if (r_kind) {
				*r_kind = klass.fields[i].kind;
			}
			return true;
		}
	}
	return false;
}

Object *Interpreter::construct(uint32_t p_class, void *p_owner) {
	if (p_class >= module_.classes.size()) {
		return nullptr;
	}
	const ir::Class &klass = module_.classes[p_class];
	if (!use_class_now(p_class)) {
		if (p_owner) {
			host_.handles.drop(p_owner);
		}
		return nullptr;
	}
	Object *object = new_object(p_class, klass.fields.size());
	if (!object) {
		if (p_owner) {
			host_.handles.drop(p_owner);
		}
		return nullptr;
	}
	for (size_t i = 0; i < klass.fields.size(); i++) {
		object->fields()[i] = empty_value(klass.fields[i].kind == Kind::F64, klass.fields[i].starts_null);
	}
	if (!klass.native_base.empty() && !klass.fields.empty() && klass.fields[0].kind == Kind::Ref && p_owner) {
		object->fields()[0].p = p_owner; // The object's own from here on.
	} else if (p_owner) {
		host_.handles.drop(p_owner);
	}
	Slot self;
	self.bits = 0;
	self.p = object;
	if (klass.constructor != NONE && !call(klass.constructor, &self, 1)) {
		release_object(object);
		return nullptr;
	}
	return object;
}

uint32_t Interpreter::find_method(uint32_t p_class, std::string_view p_name) const {
	if (p_class >= methods_.size()) {
		return NONE;
	}
	auto found = methods_[p_class].find(std::string(p_name));
	return found == methods_[p_class].end() ? NONE : found->second;
}

void Interpreter::release_object(Object *p_object) {
	if (release(p_object)) {
		drop_object(p_object);
	}
}

} // namespace vm
} // namespace kas3
