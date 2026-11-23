// Godot's methods and functions as values (lower_engine_values.cpp): `node.queueFree`, `Vector2.fromAngle`,
// `lerpf`. A helper the lowerer makes where it needs one, with the lowerer's own state.
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

class EngineValues {
	Lowerer &l_;

	// What a parameter of a forwarder is to the call it makes.
	struct Taken {
		uint32_t value = NONE; // The register handed to Godot.
		bool trimmed = false; // Declared `= null` with no default Godot can be asked for: left out when it holds nothing.
	};

	// Whether Godot's method can be called from a function of the program: every parameter and the result
	// of a kind a register holds, and no rest parameter.
	bool forwards(const Symbol *p_method) const;
	// The function of the program that calls Godot's method with the arguments it is given, made the
	// first time the method is used as a value and lowered once the function being lowered is done.
	uint32_t forwarder(Symbol *p_method, uint32_t p_position);
	// The engine object a method is read from, as `engine_receiver` gives it; NONE, refused, for `super`.
	uint32_t holder(Expr *p_expression, uint32_t p_position);
	Taken parameter(const Parameter &p_parameter, uint32_t p_register, const std::string &p_defaults, uint32_t p_index, uint32_t p_position);
	void call(const PendingFunction &p_pending, uint32_t p_receiver, const std::vector<Taken> &p_taken, uint32_t p_least, uint32_t p_position);

public:
	explicit EngineValues(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	// Godot's name for a method of one of its objects read where Godot takes a function, with the object it
	// is read from: Godot's own Callable of the two. Nothing for anything else.
	std::optional<uint32_t> callable(Expr *p_expression, Symbol *p_method);
	// One of Godot's methods or functions as a Function value. A method of an object or a value is bound to
	// what it is read from.
	uint32_t function_value(Expr *p_expression, Symbol *p_method);
	// The body of a forwarder (Functions::lower_pending_functions).
	void lower(const PendingFunction &p_pending);
};

} // namespace ir
} // namespace kas3
