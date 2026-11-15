// Where engine values come from when the interpreter holds none to ask: made new, kept by the engine
// under a name, or given by a call on no object.
//
// On failure an entry returns false (or null) and says why in r_error; the interpreter turns that into an
// AS3 error. A handle that comes back is the caller's to drop.
#pragma once

#include "engine_types.h"

#include <string>

namespace kas3 {
namespace vm {

struct EngineSource {
	virtual ~EngineSource() {}
	// A new value of one of Godot's value types, or (with no arguments) a new object of one of its classes.
	[[nodiscard]] virtual void *construct(std::string_view p_type, const Argument *p_arguments, size_t p_count, std::string *r_error) = 0;
	// `new` of a script class that extends one of Godot's classes: a new object of that Godot class, with the
	// class's script attached and p_object, which is being made, as its script's object. The handle that
	// comes back does not keep the Godot object alive (it is p_object's first field).
	[[nodiscard]] virtual void *attach_new(uint32_t p_class, Object *p_object, std::string *r_error) = 0;
	// One of Godot's collections made from a vector's elements, read where the vector holds them, all of
	// the one kind: a packed array of the type named ("PackedVector2Array"), or an Array for any other
	// name. An element that does not fit the packed type is refused; a message that starts with
	// "RangeError: " is raised as AS3's RangeError.
	[[nodiscard]] virtual void *collection(std::string_view p_type, ir::Kind p_kind, const Slot *p_elements, size_t p_count, std::string *r_error) = 0;
	// A constant the engine keeps for one of its types: Vector2's ZERO.
	[[nodiscard]] virtual bool constant(std::string_view p_class, std::string_view p_name, const Wanted &p_result, Slot *r_result,
			std::string *r_error) = 0;
	// The value of an expression in the engine's own words, as it writes a parameter's default:
	// "Color(0, 0, 0, 0)".
	[[nodiscard]] virtual bool evaluate(std::string_view p_expression, const Wanted &p_result, Slot *r_result, std::string *r_error) = 0;
	// One of the engine's singletons, by Godot's name for its class.
	[[nodiscard]] virtual void *singleton(std::string_view p_class, std::string *r_error) = 0;
	// A method called on no object: one of a value type (Vector2's from_angle) or of a class.
	[[nodiscard]] virtual bool call_static(std::string_view p_class, std::string_view p_method, const Argument *p_arguments, size_t p_count,
			const Wanted &p_result, Slot *r_result, std::string *r_error) = 0;
	// A method of a script object, as a function the engine can call. The engine's side keeps the object
	// alive for as long as that value lives. p_owner is the engine object the script object is attached
	// to, or null.
	[[nodiscard]] virtual void *bind(Object *p_object, uint32_t p_function, void *p_owner, std::string *r_error) = 0;
};

} // namespace vm
} // namespace kas3
