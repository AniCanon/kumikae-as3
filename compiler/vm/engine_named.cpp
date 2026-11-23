#include "engine_named.h"

namespace kas3 {
namespace vm {

using ir::Kind;
using ir::NONE;

void EngineNamed::construct(uint32_t p_name, const std::vector<Slot> &p_given, uint32_t p_out) {
	std::vector<Argument> arguments;
	for (const Slot each : p_given) {
		arguments.push_back(Argument{ Kind::Any, each });
	}
	std::string error;
	void *made = vm_.host_.source.construct(vm_.module_.engine_names[p_name].name, arguments.data(), arguments.size(), &error);
	if (!made) {
		raise_engine(vm_.library_, error);
		return;
	}
	// Every object of Godot's is an Object; a value type is not.
	const bool object = vm_.host_.handles.is_class(made, "Object");
	vm_.put(Kind::Any, vm_.stack_[vm_.frames_.back().base + p_out], object ? any::godot_object(made) : any::godot_value(made));
}

void *EngineNamed::handle_of(Slot p_holder) const {
	return any::is_engine(p_holder) ? any::pointer(p_holder) : engine_object_in(vm_.module_, p_holder);
}

bool EngineNamed::own_member(const ir::Class &p_class, const PropertyKey &p_key) const {
	return p_class.field(p_key.name(), true) || vm_.named_method(p_class, p_key.name()) || vm_.named_method(p_class, p_key.name(), "get ") ||
			vm_.named_method(p_class, p_key.name(), "set ");
}

Object *EngineNamed::script_with(Slot p_holder, const PropertyKey &p_key) const {
	Object *script = any::tag(p_holder) == AnyTag::Godot ? vm_.host_.handles.script_of(any::pointer(p_holder), NONE, false) : nullptr;
	return script && own_member(vm_.module_.classes[script->klass], p_key) ? script : nullptr;
}

const NamedMember &EngineNamed::member(Slot p_holder, const PropertyKey &p_key) const {
	void *handle = p_key.object ? nullptr : handle_of(p_holder);
	return handle ? vm_.host_.handles.named(handle, p_key.name()) : MISSING_MEMBER;
}

// One of Godot's methods read and not called: a function bound to the object, the AS3 name and Godot's, as
// the lowerer binds one read where the object's type is known (ir/lower_engine_values.cpp).
bool EngineNamed::method_value(void *p_handle, const PropertyKey &p_key, std::string_view p_godot, Slot *r_value) {
	const ir::Module &module = vm_.module_;
	if (module.library_function == NONE || module.array_class == NONE) {
		vm_.raise_unsupported("reading '" + std::string(p_key.name()) + "' of one of Godot's values by its name");
		return false;
	}
	Vector *bound = vm_.fresh_vector(module.array_class, 3);
	Str *name = bound ? vm_.text(p_key.name()) : nullptr;
	Str *godot = name ? vm_.text(std::string(p_godot)) : nullptr;
	if (!godot) {
		if (name) {
			free_string(name);
		}
		if (bound) {
			vm_.release_object(bound);
		}
		return false;
	}
	const bool object = vm_.host_.handles.is_class(p_handle, "Object");
	slots_of(bound)[0] = object ? any::godot_object(vm_.host_.handles.copy(p_handle)) : any::godot_value(vm_.host_.handles.copy(p_handle));
	slots_of(bound)[1] = any::string(name);
	slots_of(bound)[2] = any::string(godot);
	Object *made = vm_.make_function(module.library_function, bound);
	vm_.release_object(bound); // The function holds it now.
	*r_value = made ? any::object(made) : any::undefined();
	return made != nullptr;
}

bool EngineNamed::unknown(const PropertyKey &p_key) {
	vm_.raise(vm_.module_.error_class, "'" + std::string(p_key.name()) + "' of one of Godot's values can't be reached by its name here: this build of the extension "
										"carries no table of Godot's names. Store the value in a variable of its type.");
	return false;
}

std::optional<bool> EngineNamed::get(Slot p_holder, const PropertyKey &p_key, Slot *r_value) {
	if (Object *script = script_with(p_holder, p_key)) {
		return vm_.get_property(any::object(script), p_key, r_value);
	}
	const NamedMember &found = member(p_holder, p_key);
	void *handle = handle_of(p_holder);
	switch (found.what) {
		case NamedMember::What::Missing:
			return std::nullopt;
		case NamedMember::What::Unknown:
			return unknown(p_key);
		case NamedMember::What::Method:
			return method_value(handle, p_key, found.godot, r_value);
		case NamedMember::What::Property:
			break;
	}
	if (!found.read) {
		vm_.raise(vm_.module_.reference_error_class != NONE ? vm_.module_.reference_error_class : vm_.module_.error_class,
				"Error #1077: Illegal read of write-only property " + std::string(p_key.name()) + " on " + vm_.holder_name(p_holder) + ".");
		return false;
	}
	Wanted wanted;
	wanted.kind = Kind::Any;
	std::string error;
	if (!vm_.host_.handles.get(handle, found.godot, wanted, r_value, &error)) {
		raise_engine(vm_.library_, error);
		return false;
	}
	return true;
}

std::optional<bool> EngineNamed::set(Slot p_holder, const PropertyKey &p_key, Slot p_value) {
	if (Object *script = script_with(p_holder, p_key)) {
		return vm_.set_property(any::object(script), p_key, p_value);
	}
	const NamedMember &found = member(p_holder, p_key);
	switch (found.what) {
		case NamedMember::What::Missing:
			return std::nullopt;
		case NamedMember::What::Unknown:
			return unknown(p_key);
		case NamedMember::What::Method:
			return vm_.refuse_write("1037", "Cannot assign to a method ", p_key.name(), p_holder);
		case NamedMember::What::Property:
			break;
	}
	if (!found.written) {
		return vm_.refuse_write("1074", "Illegal write to read-only property ", p_key.name(), p_holder);
	}
	if (any::tag(p_holder) == AnyTag::Value) {
		// One of Godot's values is a copy wherever it is held: a part written here would be written to a
		// copy and lost. Said, never done.
		vm_.raise(vm_.module_.error_class, "'" + std::string(p_key.name()) + "' of one of Godot's values held untyped can't be written: the value is a copy. Store the value "
											"in a variable of its type and write to that.");
		return false;
	}
	if (const std::optional<std::string> why = vm_.godot_refuses(p_value, 0)) {
		vm_.raise(vm_.module_.type_error_class, *why);
		return false;
	}
	std::string error;
	if (!vm_.host_.handles.set(handle_of(p_holder), found.godot, Argument{ Kind::Any, p_value }, &error)) {
		raise_engine(vm_.library_, error);
		return false;
	}
	return true;
}

std::optional<bool> EngineNamed::call(Slot p_holder, const PropertyKey &p_key, const std::vector<Slot> &p_given, uint32_t p_out) {
	if (Object *script = script_with(p_holder, p_key)) {
		vm_.call_named(any::object(script), p_key, p_given, p_out);
		return !vm_.failing_;
	}
	const Object *object = vm_.held_object(p_holder);
	if (object && own_member(vm_.module_.classes[object->klass], p_key)) {
		return std::nullopt; // The script object's own member: the program's to call.
	}
	const NamedMember &found = member(p_holder, p_key);
	if (found.what == NamedMember::What::Missing) {
		return std::nullopt;
	}
	if (found.what == NamedMember::What::Unknown) {
		return unknown(p_key);
	}
	if (found.what == NamedMember::What::Property) {
		vm_.raise(vm_.module_.type_error_class, "Error #1006: " + std::string(p_key.name()) + " is not a function.");
		return false;
	}
	// Each argument as it is held; a function goes as Godot's Callable, as it does where the method is named.
	std::vector<Argument> arguments;
	std::vector<void *> callables;
	bool ok = true;
	for (size_t i = 0; ok && i < p_given.size(); i++) {
		Object *given = vm_.held_object(p_given[i]);
		if (given && given->klass == vm_.module_.function_class) {
			callables.push_back(callable(given));
			ok = callables.back() != nullptr;
			arguments.push_back(Argument{ Kind::Val, Slot{} });
			arguments.back().slot.p = callables.back();
		} else if (const std::optional<std::string> why = vm_.godot_refuses(p_given[i], 0)) {
			vm_.raise(vm_.module_.type_error_class, *why);
			ok = false;
		} else {
			arguments.push_back(Argument{ Kind::Any, p_given[i] });
		}
	}
	Wanted wanted;
	wanted.kind = Kind::Any;
	Slot result = any::undefined();
	std::string error;
	// The frame is found again by its number afterwards: Godot may run the program, which may move the stack.
	const size_t frame = vm_.frames_.size() - 1;
	ok = ok && vm_.host_.handles.call(handle_of(p_holder), found.godot, arguments.data(), arguments.size(), wanted, &result, &error);
	for (void *each : callables) {
		if (each) {
			vm_.host_.handles.drop(each);
		}
	}
	if (!ok) {
		if (!vm_.failing_) {
			raise_engine(vm_.library_, error);
		}
		return false;
	}
	if (p_out != NONE && frame < vm_.frames_.size() && !vm_.failing_) {
		vm_.put(Kind::Any, vm_.stack_[vm_.frames_[frame].base + p_out], result);
	} else {
		vm_.drop_any(result);
	}
	return !vm_.failing_;
}

void *EngineNamed::callable(Object *p_function) {
	const ir::Module &module = vm_.module_;
	std::string error;
	void *object = nullptr;
	std::string_view method;
	if (engine_method(module, p_function, &object, &method)) {
		void *made = vm_.host_.source.bind_method(object, method, &error);
		if (!made) {
			raise_engine(vm_.library_, error);
		}
		return made;
	}
	const uint32_t target = static_cast<uint32_t>(own_field(p_function, ir::FUNCTION_INDEX).i);
	const ir::Function &callee = module.functions[target];
	// A function expression that captured nothing is given nothing first: the value itself goes there.
	Object *first = static_cast<Object *>(own_field(p_function, ir::FUNCTION_FIRST).p);
	Object *bound = callee.is_static || !first ? p_function : first;
	const ir::Class &klass = module.classes[bound->klass];
	void *owner = !callee.is_static && !klass.native_base.empty() && !klass.fields.empty() ? owner_field(bound).p : nullptr;
	void *made = vm_.host_.source.bind(bound, target, owner, &error);
	if (!made) {
		vm_.raise(module.error_class, error.empty() ? "the engine could not take this function" : error);
	}
	return made;
}

bool EngineNamed::has(Slot p_holder, const PropertyKey &p_key) const {
	return script_with(p_holder, p_key) || member(p_holder, p_key).what != NamedMember::What::Missing;
}

} // namespace vm
} // namespace kas3
