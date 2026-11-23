// What an untyped value does with Godot by name, found out as the program runs (vm/engine_named.cpp): a
// class of Godot's held as a class value made new, and a member of one of Godot's values or objects read,
// written or called by its AS3 name (`untyped.name`, `untyped.queueFree()`). One helper class, a friend of
// the interpreter, so that the interpreter's own header does not grow. Nothing on the path of typed code
// is here.
//
// The value is one of Godot's own, or a script object attached to one of Godot's objects, whose own members
// come first. A node that carries a script of the program is that script's object for the names the
// script has, as it is to GDScript.
#pragma once

#include "interpreter_internal.h"

#include <optional>
#include <vector>

namespace kas3 {
namespace vm {

class EngineNamed {
	Interpreter &vm_;

	// The handle the value reaches Godot by: its own, or the Godot object a script object is attached to.
	void *handle_of(Slot p_holder) const;
	// The script object the Godot object an untyped value holds carries, if it has a member of that name.
	Object *script_with(Slot p_holder, const PropertyKey &p_key) const;
	NamedMember member(Slot p_holder, const PropertyKey &p_key) const;
	bool method_value(void *p_handle, const PropertyKey &p_key, std::string_view p_godot, Slot *r_value);
	bool unknown(const PropertyKey &p_key);

public:
	explicit EngineNamed(Interpreter &p_vm) :
			vm_(p_vm) {}

	// `new c(a)` where c is one of Godot's classes (its engine name): what `new` of the class named asks of
	// the engine, a new object of the class or a new value of the value type, with the arguments as they
	// are held. The running frame's register p_out takes it, untyped.
	void construct(uint32_t p_name, const std::vector<Slot> &p_given, uint32_t p_out);

	// Each is asked of a value that is one of Godot's or a script object attached to one, and answers
	// nothing where Godot has no member of that name: the caller goes on as it does for any value. An
	// answer of false has raised.
	std::optional<bool> get(Slot p_holder, const PropertyKey &p_key, Slot *r_value);
	std::optional<bool> set(Slot p_holder, const PropertyKey &p_key, Slot p_value);
	std::optional<bool> call(Slot p_holder, const PropertyKey &p_key, const std::vector<Slot> &p_given, uint32_t p_out);
	bool has(Slot p_holder, const PropertyKey &p_key) const;
	// A function value as Godot's Callable, a new handle: bound to what the function is called with first (a
	// method's object, a function expression's environment), or to the function value itself for one that
	// takes nothing first; one of Godot's own methods is Godot's own Callable of its object. A method's is
	// the one its object would give, so that adding and removing a listener match. Null if it raised.
	void *callable(Object *p_function);
	// Whether a class's objects have a public member of their own of that name.
	bool own_member(const ir::Class &p_class, const PropertyKey &p_key) const;
};

} // namespace vm
} // namespace kas3
