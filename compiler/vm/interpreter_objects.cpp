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

bool Interpreter::is_class(const Object *p_object, uint32_t p_class) const {
	if (!p_object) {
		return false;
	}
	if (module_.classes[p_class].is_interface) {
		const std::vector<uint32_t> &implemented = module_.classes[p_object->klass].interfaces;
		return std::find(implemented.begin(), implemented.end(), p_class) != implemented.end();
	}
	for (uint32_t at = p_object->klass; at != NONE; at = module_.classes[at].base) {
		if (at == p_class) {
			return true;
		}
	}
	return false;
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

std::string Interpreter::object_text(Object *p_object) const {
	if (!p_object) {
		return "null";
	}
	const std::string &name = module_.classes[p_object->klass].name;
	std::string simple = name.substr(name.rfind('.') == std::string::npos ? 0 : name.rfind('.') + 1);
	if (module_.error_class != NONE && is_class(p_object, module_.error_class)) {
		return error_heading(p_object); // A class of the program's that extends Error is named "Error".
	}
	if (p_object->klass == module_.date_class) {
		return date_text(p_object, ir::DateText::Full);
	}
	if (p_object->klass == module_.function_class) {
		return "function Function() {}"; // avmplus's Function.as: no source text is kept, for any function.
	}
	if (p_object->klass == module_.class_class) {
		return "[class " + class_name(static_cast<uint32_t>(p_object->fields()[ir::CLASS_CODE].i)) + "]";
	}
	if (p_object->klass == module_.regexp_class) {
		const Str *source = static_cast<const Str *>(p_object->fields()[ir::REGEXP_SOURCE].p);
		return regex::regexp_text(source ? source->view() : std::string_view(), static_cast<uint8_t>(p_object->fields()[ir::REGEXP_FLAGS].i));
	}
	return "[object " + simple + "]";
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
	*r_value = p_object->fields()[*field];
	if (r_kind) {
		*r_kind = klass.fields[*field].kind;
	}
	return true;
}

void Interpreter::set_field(Object *p_object, uint32_t p_field, Slot p_owned) {
	put(module_.classes[p_object->klass].fields[p_field].kind, p_object->fields()[p_field], p_owned);
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
	Object *object = new_object(p_class, klass.fields.size(), heap_.list_for(p_class));
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

} // namespace vm
} // namespace kas3
