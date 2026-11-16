// What the interpreter does with an engine value it holds: one of Godot's values (a Vector2) or objects
// (a node), which to the interpreter is a handle and nothing more.
//
// On failure an entry returns false (or null) and says why in r_error; the interpreter turns that into an
// AS3 error. A string that comes back is a new Str, and a handle is a new handle: both the caller's.
// r_result is null when the result is not wanted. A message that starts with "TypeError: " is raised as
// AS3's TypeError, which is how a member reached on a freed object is reported.
#pragma once

#include "engine_types.h"

#include <string>

namespace kas3 {
namespace vm {

struct EngineHandles {
	virtual ~EngineHandles() {}
	// Another handle for the same value (for a value type, a copy of it), and giving one up.
	[[nodiscard]] virtual void *copy(void *p_value) = 0;
	virtual void drop(void *p_value) = 0;
	// Whether a handle still has something behind it: not if it is null, or an object the engine has freed.
	// A freed object is null to a script.
	[[nodiscard]] virtual bool present(void *p_value) = 0;
	// Whether a handle is an object of the class of that name (Godot's), or of a class that extends it;
	// for one of Godot's values, whether it is of the value type of that name. Not if null or freed.
	[[nodiscard]] virtual bool is_class(void *p_value, std::string_view p_class) = 0;
	// The engine's own text for a value or an object. Null only when there was no memory for it.
	[[nodiscard]] virtual Str *text(void *p_value) = 0;
	// The program let go of its last reference to a script object whose Godot object is reference-counted
	// (ir::Class::counted). True if the engine still holds that object: the script object is then kept,
	// with what it holds, for as long as the engine does, and given back with Interpreter::free_kept().
	// False to free it now.
	[[nodiscard]] virtual bool keep(Object *p_object) = 0;

	[[nodiscard]] virtual bool call(void *p_receiver, std::string_view p_method, const Argument *p_arguments, size_t p_count,
			const Wanted &p_result, Slot *r_result, std::string *r_error) = 0;
	[[nodiscard]] virtual bool get(void *p_receiver, std::string_view p_property, const Wanted &p_result, Slot *r_result, std::string *r_error) = 0;
	[[nodiscard]] virtual bool set(void *p_receiver, std::string_view p_property, const Argument &p_value, std::string *r_error) = 0;
	// A member of an engine object by its AS3 name, known only as the program runs: `timer[name]`.
	[[nodiscard]] virtual bool member(void *p_receiver, std::string_view p_name, const Wanted &p_result, Slot *r_result, std::string *r_error) = 0;
	// An element, by a number or another key: `vector[0]`.
	[[nodiscard]] virtual bool index_get(void *p_receiver, const Argument &p_key, const Wanted &p_result, Slot *r_result, std::string *r_error) = 0;
	[[nodiscard]] virtual bool index_set(void *p_receiver, const Argument &p_key, const Argument &p_value, std::string *r_error) = 0;
	// An operator as AS3 writes it ("+", "==", "<"), on one operand or two, at least one of them an engine
	// value.
	[[nodiscard]] virtual bool operate(std::string_view p_operator, const Argument *p_arguments, size_t p_count, const Wanted &p_result,
			Slot *r_result, std::string *r_error) = 0;

	// For the cycle collector (vm/collector.h), which runs between calls into the program.
	// The node a handle on one of the engine's objects holds a counted reference to (EngineNode), or 0 if
	// it holds none: the collector then does not look past it.
	[[nodiscard]] virtual uint64_t node(void *p_handle) = 0;
	// What a node holds now. False if it is gone.
	[[nodiscard]] virtual bool node_edges(uint64_t p_node, EngineNode *r_node) = 0;
	// These nodes, whose counts were not exact, are held only by one another and by script objects that are
	// about to be freed. The engine lets go of what holds them that it can't count (a listener's signal),
	// and so of what they hold. If one of them outlives that (something else held a copy), everything is
	// put back as it was, in the same order, and those that outlived it are returned; the collector then
	// keeps them and what they hold.
	[[nodiscard]] virtual std::vector<uint64_t> let_go(const std::vector<uint64_t> &p_nodes) = 0;
};

} // namespace vm
} // namespace kas3
