#include "engine_named.h"

namespace kas3 {
namespace vm {

namespace {

Argument receiver(Slot p_holder, void *p_handle) {
	Argument argument{ any::tag(p_holder) == AnyTag::Value ? ir::Kind::Val : ir::Kind::Ref, Slot{} };
	argument.slot.p = p_handle;
	return argument;
}

std::optional<bool> prepared(EngineHandles &p_host, const Object *p_checked, const Prepared *p_prepared, const Argument *p_arguments, size_t p_count, const Wanted &p_wanted,
		Slot *r_value, std::string *r_error) {
	return p_prepared ? p_host.run(*p_prepared, p_arguments, p_count, p_wanted, r_value, r_error, p_checked) : std::nullopt;
}

} // namespace

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

const NamedMember &EngineNamed::member(Slot p_holder, const PropertyKey &p_key) const {
	void *handle = p_key.object ? nullptr : handle_of(p_holder);
	if (!handle) {
		return MISSING_MEMBER;
	}
	const NamedMember &found = vm_.host_.handles.named(handle, p_key.name(), NameSource::Computed);
	if (found.what == NamedMember::What::Missing && !checked_) {
		return vm_.host_.handles.script_member(handle, p_key.name());
	}
	return found;
}

// One of Godot's methods read and not called: a function bound to the object, the AS3 name and Godot's, as
// the lowerer binds one read where the object's type is known (ir/lower_engine_values.cpp).
bool EngineNamed::method_value(Slot p_holder, const PropertyKey &p_key, std::string_view p_godot, Slot *r_value, MethodNames *p_names) {
	MethodNames local;
	MethodNames &names = p_names ? *p_names : local;
	if (!names.as3) {
		names.as3 = vm_.text(p_key.name());
	}
	if (names.as3 && !names.godot) {
		names.godot = vm_.text(p_godot);
	}
	return names.godot && bind_method(p_holder, names.as3, names.godot, r_value);
}

bool EngineNamed::bind_method(Slot p_holder, Str *p_as3, Str *p_godot, Slot *r_value) {
	const ir::Module &module = vm_.module_;
	if (module.function_class == NONE || module.library_function == NONE || module.array_class == NONE) {
		vm_.raise_unsupported("reading '" + std::string(p_as3->view()) + "' of " + vm_.holder_name(p_holder) + " by its name");
		return false;
	}
	const auto [function, bound] = new_method_storage(module.function_class, slots_of(module.classes[module.function_class]),
			vm_.heap_.list_for(module.function_class), module.array_class, vm_.heap_.list_for(module.array_class));
	if (!function) {
		vm_.out_of_memory();
		return false;
	}
	const bool object = any::tag(p_holder) != AnyTag::Value;
	void *receiver = vm_.host_.handles.copy(handle_of(p_holder));
	slots_of(bound)[0] = object ? any::godot_object(receiver) : any::godot_value(receiver);
	retain(p_as3);
	retain(p_godot);
	slots_of(bound)[1] = any::string(p_as3);
	slots_of(bound)[2] = any::string(p_godot);
	own_field(function, ir::FUNCTION_INDEX).i = static_cast<int32_t>(module.library_function);
	own_field(function, ir::FUNCTION_FIRST).p = bound;
	*r_value = any::object(function);
	return true;
}

bool EngineNamed::unknown(const PropertyKey &p_key) {
	vm_.raise(vm_.module_.error_class, "'" + std::string(p_key.name()) + "' of one of Godot's values can't be reached by its name here: this build of the extension "
										"carries no table of Godot's names. Store the value in a variable of its type.");
	return false;
}

std::optional<bool> EngineNamed::get(Slot p_holder, const PropertyKey &p_key, Slot *r_value) {
	const NamedMember &found = member(p_holder, p_key);
	if (found.what == NamedMember::What::Missing) {
		return std::nullopt;
	}
	return read(found, p_holder, p_key, r_value);
}

bool EngineNamed::read(const NamedMember &p_member, Slot p_holder, const PropertyKey &p_key, Slot *r_value) {
	void *handle = handle_of(p_holder);
	switch (p_member.what) {
		case NamedMember::What::Missing:
			return false;
		case NamedMember::What::Unknown:
			return unknown(p_key);
		case NamedMember::What::Method:
			return method_value(p_holder, p_key, p_member.godot, r_value, p_member.names);
		case NamedMember::What::Property:
			break;
	}
	if (!p_member.read) {
		vm_.raise(vm_.module_.reference_error_class != NONE ? vm_.module_.reference_error_class : vm_.module_.error_class,
				"Error #1077: Illegal read of write-only property " + std::string(p_key.name()) + " on " + vm_.holder_name(p_holder) + ".");
		return false;
	}
	Wanted wanted;
	wanted.kind = Kind::Any;
	std::string error;
	const Argument operand = receiver(p_holder, handle);
	const auto done = prepared(vm_.host_.handles, checked_, p_member.script_only ? nullptr : p_member.invoke, &operand, 1, wanted, r_value, &error);
	if (!(done ? *done : vm_.host_.handles.get(handle, p_member.godot, wanted, r_value, &error))) {
		raise_engine(vm_.library_, error);
		return false;
	}
	return true;
}

std::optional<bool> EngineNamed::set(Slot p_holder, const PropertyKey &p_key, Slot p_value) {
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
	void *handle = handle_of(p_holder);
	const Argument operands[] = { receiver(p_holder, handle), { Kind::Any, p_value } };
	const auto done = prepared(vm_.host_.handles, checked_, found.setter, operands, 2, Wanted{}, nullptr, &error);
	if (!(done ? *done : vm_.host_.handles.set(handle, found.godot, operands[1], &error))) {
		raise_engine(vm_.library_, error);
		return false;
	}
	return true;
}

std::optional<bool> EngineNamed::call(Slot p_holder, const PropertyKey &p_key, const std::vector<Slot> &p_given, uint32_t p_out) {
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
	return call_bound(p_holder, found.godot, found.script_only ? nullptr : found.invoke, p_given, p_out);
}

bool EngineNamed::call_bound(Slot p_holder, std::string_view p_godot, const Prepared *p_binding, const std::vector<Slot> &p_given, uint32_t p_out) {
	// Each argument as it is held; a function goes as Godot's Callable, as it does where the method is named.
	const size_t count = p_given.size() + 1;
	Argument few[16];
	std::vector<Argument> many(count > 16 ? count : 0);
	Argument *arguments = many.empty() ? few : many.data();
	void *handle = handle_of(p_holder);
	arguments[0] = receiver(p_holder, handle);
	size_t ready = 1;
	for (; ready < count; ready++) {
		const Slot value = p_given[ready - 1];
		Object *given = vm_.held_object(value);
		if (given && given->klass == vm_.module_.function_class) {
			arguments[ready] = { Kind::Val, Slot{} };
			arguments[ready].slot.p = callable(given);
			if (!arguments[ready].slot.p) {
				break;
			}
		} else if (const std::optional<std::string> why = vm_.godot_refuses(value, 0)) {
			vm_.raise(vm_.module_.type_error_class, *why);
			break;
		} else {
			arguments[ready] = { Kind::Any, value };
		}
	}
	const Wanted wanted{ Kind::Any, {} };
	Slot result = any::undefined();
	std::string error;
	const size_t frame = vm_.frames_.size() - 1;
	bool ok = ready == count;
	if (ok) {
		const auto done = prepared(vm_.host_.handles, checked_, p_binding, arguments, count, wanted, &result, &error);
		ok = done ? *done : vm_.host_.handles.call(handle, p_godot, arguments + 1, count - 1, wanted, &result, &error);
	}
	for (size_t i = 1; i < ready; i++) {
		if (arguments[i].kind == Kind::Val) {
			vm_.host_.handles.drop(arguments[i].slot.p);
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
	void *handle = p_key.object ? nullptr : handle_of(p_holder);
	return handle && (vm_.host_.handles.named(handle, p_key.name()).what != NamedMember::What::Missing ||
			(!checked_ && vm_.host_.handles.script_has(handle, p_key.name())));
}

} // namespace vm
} // namespace kas3
