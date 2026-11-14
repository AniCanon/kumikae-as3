// The engine's objects, as the interpreter uses them: methods, properties, signals, singletons.
//
// On failure an entry returns false (or null) and says why in r_error; the interpreter turns that into an
// AS3 error. r_result is null when the result is not wanted.
#pragma once

#include "engine_types.h"

#include <string>

namespace kas3 {
namespace vm {

struct EngineObjects {
	virtual ~EngineObjects() {}
	[[nodiscard]] virtual bool call(void *p_receiver, std::string_view p_method, const Argument *p_arguments, size_t p_count,
			const Wanted &p_result, Slot *r_result, std::string *r_error) = 0;
	[[nodiscard]] virtual bool get(void *p_receiver, std::string_view p_property, const Wanted &p_result, Slot *r_result, std::string *r_error) = 0;
	[[nodiscard]] virtual bool set(void *p_receiver, std::string_view p_property, const Argument &p_value, std::string *r_error) = 0;
	// A member of an engine object by its AS3 name, known only as the program runs: `timer[name]`.
	[[nodiscard]] virtual bool member(void *p_receiver, std::string_view p_name, const Wanted &p_result, Slot *r_result, std::string *r_error) = 0;
	// One of the engine's singletons, by Godot's name for its class. The handle is the caller's to drop.
	[[nodiscard]] virtual void *singleton(std::string_view p_class, std::string *r_error) = 0;
	// A method called on no object: one of a value type (Vector2's from_angle) or of a class.
	[[nodiscard]] virtual bool call_static(std::string_view p_class, std::string_view p_method, const Argument *p_arguments, size_t p_count,
			const Wanted &p_result, Slot *r_result, std::string *r_error) = 0;
	// A method bound to its object, as a function the engine can call: a handle on an engine value. The
	// engine's side keeps the object alive for as long as that value lives. p_owner is the engine object
	// the script object is attached to, or null.
	[[nodiscard]] virtual void *bind(Object *p_object, uint32_t p_function, void *p_owner, std::string *r_error) = 0;
};

} // namespace vm
} // namespace kas3
