// The engine's values, as the interpreter uses them: made, copied, dropped, operated on, indexed.
//
// On failure an entry returns false (or null) and says why in r_error; the interpreter turns that into an
// AS3 error. A string that comes back is a new Str, and a handle is a new handle: both the caller's.
#pragma once

#include "engine_types.h"

#include <string>

namespace kas3 {
namespace vm {

struct EngineValues {
	virtual ~EngineValues() {}
	// A new value of one of Godot's value types.
	[[nodiscard]] virtual void *construct(std::string_view p_type, const Argument *p_arguments, size_t p_count, std::string *r_error) = 0;
	// Another handle for the same value (for a value type, a copy of it), and giving one up.
	[[nodiscard]] virtual void *copy(void *p_value) = 0;
	virtual void drop(void *p_value) = 0;
	// The engine's own text for a value or an object.
	[[nodiscard]] virtual Str *text(void *p_value) = 0;
	// An operator as AS3 writes it ("+", "==", "<"), on one operand or two, at least one of them an engine
	// value.
	[[nodiscard]] virtual bool operate(std::string_view p_operator, const Argument *p_arguments, size_t p_count, const Wanted &p_result,
			Slot *r_result, std::string *r_error) = 0;
	// An element of an engine value, by a number or another key: `vector[0]`.
	[[nodiscard]] virtual bool index_get(void *p_receiver, const Argument &p_key, const Wanted &p_result, Slot *r_result, std::string *r_error) = 0;
	[[nodiscard]] virtual bool index_set(void *p_receiver, const Argument &p_key, const Argument &p_value, std::string *r_error) = 0;
	// A constant the engine keeps for one of its types: Vector2's ZERO.
	[[nodiscard]] virtual bool constant(std::string_view p_class, std::string_view p_name, const Wanted &p_result, Slot *r_result,
			std::string *r_error) = 0;
};

} // namespace vm
} // namespace kas3
