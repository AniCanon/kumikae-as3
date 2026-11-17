#include "interpreter_internal.h"

#include "regex.h"
#include "table.h"

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

// Null, and the error raised, when there is no memory for it.
Str *Interpreter::text(const std::string &p_text) {
	Str *made = new_string(p_text);
	if (!made) {
		out_of_memory();
	}
	return made;
}

uint32_t Interpreter::interface_function(const Object *p_object, uint32_t p_method) {
	if (!p_object) {
		raise_null();
		return NONE;
	}
	const std::vector<uint32_t> &table = module_.classes[p_object->klass].interface_methods;
	const uint32_t function = p_method < table.size() ? table[p_method] : NONE;
	if (function == NONE) {
		const std::string name = p_method < module_.interface_methods.size() ? module_.interface_methods[p_method] : std::string("a method");
		raise(module_.type_error_class, "Error #1006: " + name + " is not a function.");
	}
	return function;
}

bool Interpreter::get_field(Object *p_object, std::string_view p_name, Slot *r_value, Kind *r_kind) const {
	if (!p_object) {
		return false;
	}
	const ir::Class &klass = module_.classes[p_object->klass];
	const std::optional<uint32_t> field = klass.field(p_name);
	if (!field) {
		return false;
	}
	*r_value = vm::field(klass, p_object, *field);
	if (r_kind) {
		*r_kind = klass.fields[*field].kind;
	}
	return true;
}

void Interpreter::set_field(Object *p_object, uint32_t p_field, Slot p_owned) {
	const ir::Class &klass = module_.classes[p_object->klass];
	put(klass.fields[p_field].kind, field(klass, p_object, p_field), p_owned);
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
	Object *object = new_object(p_class, heap_.object_slots(p_class), heap_.list_for(p_class));
	if (!object) {
		if (p_owner) {
			host_.handles.drop(p_owner);
		}
		return nullptr;
	}
	reset_fields(klass, object);
	if (!klass.native_base.empty() && !klass.fields.empty() && klass.fields[0].kind == Kind::Ref && p_owner) {
		owner_field(object).p = p_owner; // The object's own from here on.
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

} // namespace vm
} // namespace kas3
