// A class held as a value gives its public static members by name (`c.create()`, `c.count`): its static
// methods, getters and setters, which are functions named after the class ("Maker/create", "Maker/get
// label"), and its static fields, which are globals ("Maker.made"). A base class's are not its subclass's:
// AS3's statics are not inherited. The class is set up before any of them is reached, as a class named is.
#include "interpreter_internal.h"

namespace kas3 {
namespace vm {

using ir::Kind;
using ir::NONE;

// The public static function or field of that name ("/create", "/get label", ".made"), found once for the
// whole program.
std::optional<uint32_t> Interpreter::static_member(uint32_t p_class, char p_joint, std::string_view p_prefix, std::string_view p_name) {
	if (!statics_listed_) {
		statics_listed_ = true;
		for (size_t i = 0; i < module_.functions.size(); i++) {
			const ir::Function &function = module_.functions[i];
			if (function.is_static && function.is_public && function.owner != NONE) {
				statics_.emplace(function.name, static_cast<uint32_t>(i));
			}
		}
		for (size_t i = 0; i < module_.globals.size(); i++) {
			if (module_.globals[i].is_public) {
				statics_.emplace(module_.globals[i].name, static_cast<uint32_t>(i));
			}
		}
	}
	std::string key = module_.classes[p_class].name;
	key += p_joint;
	key += p_prefix;
	key += p_name;
	const auto found = statics_.find(key);
	return found == statics_.end() ? std::nullopt : std::optional<uint32_t>(found->second);
}

// The class of the program a class value names, set up; nothing for any other object, or if setting it up
// raised (*r_raised says which).
std::optional<uint32_t> Interpreter::statics_of(const Object *p_object, bool *r_raised) {
	*r_raised = false;
	if (!p_object || module_.class_class == NONE || p_object->klass != module_.class_class) {
		return std::nullopt;
	}
	const uint32_t code = static_cast<uint32_t>(const_cast<Object *>(p_object)->fields()[ir::CLASS_CODE].i);
	if (code >= module_.classes.size()) {
		return std::nullopt;
	}
	if (!class_used_[code] && !use_class_now(code)) {
		*r_raised = true;
		return std::nullopt;
	}
	return code;
}

std::optional<bool> Interpreter::static_get(Object *p_object, const PropertyKey &p_key, Slot p_holder, Slot *r_value) {
	bool raised = false;
	const std::optional<uint32_t> code = statics_of(p_object, &raised);
	if (!code) {
		return raised ? std::optional<bool>(false) : std::nullopt;
	}
	if (const std::optional<uint32_t> field = static_member(*code, '.', "", p_key.text)) {
		*r_value = to_any(module_.globals[*field].kind, native_ ? *native_->global(*field) : globals_[*field]);
		return true;
	}
	if (const std::optional<uint32_t> method = static_member(*code, '/', "", p_key.text)) {
		Object *made = make_function(*method, nullptr);
		*r_value = made ? any::object(made) : any::undefined();
		return made != nullptr;
	}
	if (const std::optional<uint32_t> getter = static_member(*code, '/', "get ", p_key.text)) {
		Object *function = make_function(*getter, nullptr);
		const std::optional<Slot> given = function ? call_function_within(function, nullptr, 0) : std::nullopt;
		release_object(function);
		if (given) {
			*r_value = *given;
		}
		return given.has_value();
	}
	if (static_member(*code, '/', "set ", p_key.text)) {
		raise(module_.reference_error_class != NONE ? module_.reference_error_class : module_.error_class,
				"Error #1077: Illegal read of write-only property " + p_key.text + " on " + holder_name(p_holder) + ".");
		return false;
	}
	return std::nullopt;
}

std::optional<bool> Interpreter::static_set(Object *p_object, const PropertyKey &p_key, Slot p_holder, Slot p_value) {
	bool raised = false;
	const std::optional<uint32_t> code = statics_of(p_object, &raised);
	if (!code) {
		return raised ? std::optional<bool>(false) : std::nullopt;
	}
	if (const std::optional<uint32_t> field = static_member(*code, '.', "", p_key.text)) {
		const ir::Global &declared = module_.globals[*field];
		if (declared.is_const) {
			return refuse_write("1074", "Illegal write to read-only property ", p_key.text, p_holder);
		}
		if (ir::is_engine(declared.kind)) {
			raise_unsupported("writing a field of one of Godot's types by its name");
			return false;
		}
		Slot stored;
		if (!from_any(p_value, declared.kind, declared.kind == Kind::Obj ? declared.klass : declared.starts_null ? 0 : NONE, &stored)) {
			return false;
		}
		put(declared.kind, native_ ? *native_->global(*field) : globals_[*field], stored);
		return true;
	}
	if (static_member(*code, '/', "", p_key.text)) {
		return refuse_write("1037", "Cannot assign to a method ", p_key.text, p_holder);
	}
	if (const std::optional<uint32_t> setter = static_member(*code, '/', "set ", p_key.text)) {
		Object *function = make_function(*setter, nullptr);
		const Slot value = share(Kind::Any, p_value); // Not the stack's: the call may move it.
		const std::optional<Slot> given = function ? call_function_within(function, &value, 1) : std::nullopt;
		drop_any(value);
		release_object(function);
		if (given) {
			drop_any(*given);
		}
		return given.has_value();
	}
	if (static_member(*code, '/', "get ", p_key.text)) {
		return refuse_write("1074", "Illegal write to read-only property ", p_key.text, p_holder);
	}
	return refuse_write("1056", "Cannot create property ", p_key.text, p_holder);
}

// Whether the class a class value names has a public static member of that name.
bool Interpreter::static_has(const Object *p_object, const PropertyKey &p_key) {
	bool raised = false;
	const std::optional<uint32_t> code = statics_of(p_object, &raised);
	return code && (static_member(*code, '.', "", p_key.text) || static_member(*code, '/', "", p_key.text) ||
			static_member(*code, '/', "get ", p_key.text) || static_member(*code, '/', "set ", p_key.text));
}

// c.name(arguments) where it is a static method: called. False for anything else, which the caller goes on with.
bool Interpreter::static_call(Object *p_object, const PropertyKey &p_key, const std::vector<Slot> &p_given, uint32_t p_out) {
	bool raised = false;
	const std::optional<uint32_t> code = statics_of(p_object, &raised);
	if (raised) {
		return true;
	}
	const std::optional<uint32_t> method = code ? static_member(*code, '/', "", p_key.text) : std::nullopt;
	if (method) {
		call_with(*method, nullptr, p_given, p_out);
	}
	return method.has_value();
}

} // namespace vm
} // namespace kas3
