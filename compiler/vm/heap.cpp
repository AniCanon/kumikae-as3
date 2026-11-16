#include "heap.h"

#include "any.h"
#include "constructions.h"
#include "held.h"
#include "table.h"

#include <cstdlib>

namespace kas3 {
namespace vm {

using ir::Kind;

Heap::Heap(const ir::Module &p_module, EngineHandles &p_handles, EngineSource &p_source, Clock &p_clock) :
		module(p_module), handles(p_handles), source(p_source), clock(p_clock), collector(*this) {
	methods_.resize(module.classes.size());
	for (size_t i = 0; i < module.classes.size(); i++) {
		for (uint32_t method : module.classes[i].methods) {
			const std::string &name = module.functions[method].name;
			methods_[i][name.substr(name.rfind('/') == std::string::npos ? 0 : name.rfind('/') + 1)] = method;
		}
	}
	operators_.resize(module.engine_names.size());
	for (size_t i = 0; i < module.engine_names.size(); i++) {
		// The description's names for the unary ones are "unary-" and "unary+".
		std::string_view spelling = module.engine_names[i].name;
		if (spelling.substr(0, 5) == "unary") {
			spelling.remove_prefix(5);
		}
		// An operator's entry is "<name> <Godot's number>"; anything else is not an operator and has none.
		const std::string &value = module.engine_names[i].value;
		const size_t space = value.find(' ');
		const bool numbered = module.engine_names[i].key.rfind("@operator.", 0) == 0 && space != std::string::npos;
		operators_[i] = OperatorName{ spelling, spelling == "==" || spelling == "!=", numbered ? std::atoi(value.c_str() + space + 1) : -1 };
	}
}

Object *&Heap::lone_function(uint32_t p_function) {
	if (lone_functions_.size() <= p_function) {
		lone_functions_.resize(module.functions.size(), nullptr);
	}
	return lone_functions_[p_function];
}

void Heap::release_lone_functions() {
	for (Object *&kept : lone_functions_) {
		Object *held = kept;
		kept = nullptr;
		release_object(held);
	}
}

uint32_t Heap::find_method(uint32_t p_class, std::string_view p_name) const {
	if (p_class >= methods_.size()) {
		return ir::NONE;
	}
	auto found = methods_[p_class].find(std::string(p_name));
	return found == methods_[p_class].end() ? ir::NONE : found->second;
}

// Gives up the reference a slot holds.
void Heap::drop(Kind p_kind, Slot &p_slot) {
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
		handles.drop(handle);
		return;
	} else if (p_kind == Kind::Any) {
		const Slot value = p_slot;
		p_slot.bits = 0; // For the same reason.
		drop_any(value);
		return;
	}
	p_slot.bits = 0;
}

void Heap::drop_any(Slot p_value) {
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
			handles.drop(any::pointer(p_value));
			break;
		default:
			break;
	}
}

Slot Heap::share(Kind p_kind, Slot p_value) {
	if (p_kind == Kind::Any) {
		retain(any::counted(p_value));
		if (any::tag(p_value) == AnyTag::Value) {
			return any::godot_value(handles.copy(any::pointer(p_value)));
		}
		if (any::tag(p_value) == AnyTag::Godot) {
			return any::godot_object(handles.copy(any::pointer(p_value)));
		}
		return p_value;
	}
	if (ir::is_reference(p_kind)) {
		retain(static_cast<Counted *>(p_value.p));
	} else if (ir::is_engine(p_kind) && p_value.p) {
		p_value.p = handles.copy(p_value.p);
	}
	return p_value;
}

void Heap::put(Kind p_kind, Slot &p_target, Slot p_owned) {
	Slot old = p_target;
	p_target = p_owned;
	if (ir::is_held(p_kind)) {
		drop(p_kind, old);
	}
}

// Frees an object whose last reference is gone, and whatever that in turn leaves unreferenced. A list
// rather than recursion: a long chain of objects must not overflow the C++ stack. Letting go of an engine
// value can run code that frees more objects: those join the list of the call already freeing, which keeps
// one list for every call and allocates nothing once it has grown.
void Heap::drop_object(Object *p_object) {
	dying_.push_back(p_object);
	if (freeing_) {
		return;
	}
	freeing_ = true;
	while (!dying_.empty()) {
		Object *object = dying_.back();
		dying_.pop_back();
		const ir::Class &klass = module.classes[object->klass];
		if (klass.counted && handles.keep(object)) {
			continue; // The engine still holds its Godot object, and keeps it.
		}
		std::vector<Entry> taken = take_entries(klass, object);
		each_held(klass, object, [this](Kind p_kind, Slot &p_slot) { let_go(p_kind, p_slot); });
		for (Entry &entry : taken) {
			let_go(Kind::Any, entry.key);
			let_go(Kind::Any, entry.value);
		}
		if (klass.table) {
			free_table(object);
		} else if (klass.element != Kind::Void) {
			free_properties(static_cast<Vector *>(object));
			free_vector(static_cast<Vector *>(object));
		} else {
			if (constructions) {
				constructions->forget(object);
			}
			free_object(object);
		}
	}
	freeing_ = false;
}

// Empties one slot of an object being freed. An object it held whose last reference this was goes on the
// list of those to free, as drop_object() keeps it.
void Heap::let_go(Kind p_kind, Slot &p_slot) {
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

void Heap::assign(Kind p_kind, Slot &p_slot, void *p_value) {
	Slot old = p_slot;
	p_slot.bits = 0;
	p_slot.p = p_value;
	drop(p_kind, old);
}

void Heap::release_object(Object *p_object) {
	if (release(p_object)) {
		drop_object(p_object);
	}
}

} // namespace vm
} // namespace kas3
