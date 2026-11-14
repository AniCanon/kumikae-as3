// What the interpreter does with an engine value it holds: one of Godot's values (a Vector2) or objects
// (a node), which to the interpreter is a handle and nothing more.
//
// On failure an entry returns false (or null) and says why in r_error; the interpreter turns that into an
// AS3 error. A string that comes back is a new Str, and a handle is a new handle: both the caller's.
// r_result is null when the result is not wanted.
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
	// The engine's own text for a value or an object.
	[[nodiscard]] virtual Str *text(void *p_value) = 0;

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
};

} // namespace vm
} // namespace kas3
