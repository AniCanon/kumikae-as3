// Godot's members, values and operators, reached from the code being lowered (lower_engine.cpp): the object
// a member is reached through, properties and constants as places, singletons, the zero of a value with
// parts, 32-bit integers, and operators that Godot carries out. A helper the lowerer makes where it needs
// one, with the lowerer's own state.
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

class Engine {
	Lowerer &l_;

	// One of Godot's constants. One with a value in the declarations (`Key.ESCAPE`) is that value; any
	// other (`Vector2.ZERO`) is asked of the engine.
	Place constant_place(Symbol *p_symbol, uint32_t p_position);

public:
	explicit Engine(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	// The engine object a Godot member is reached through: the script's own owner when nothing is
	// written before the name, or the value of what is written.
	uint32_t engine_receiver(Expr *p_object, uint32_t p_position, bool *r_of_value, uint32_t *r_script_object = nullptr);
	void owner_of(uint32_t p_script_object, uint32_t p_owner, uint32_t p_position);

	// One of Godot's values that is made of parts (a Vector2's x and y, a Color's r, g, b and a) is never
	// missing unless a program says so: a variable of it with no value written starts at the type's zero,
	// as it does in GDScript. This gives that zero in a new register, or NONE for any other type (an array,
	// a signal, a function), which starts missing as an object does.
	uint32_t zero_value(TypeRef *p_written, uint32_t p_position, uint32_t p_into = NONE);

	// One of Godot's values that stores 32-bit integers: every part of it is an `int`, or is such a value
	// (Vector2i; Rect2i, which is two of them). Godot cuts a wider integer off inside these without a
	// word, so a whole number on its way into one goes as an `int`, and says so if it does not fit.
	bool stores_int32(const Type *p_type) const;

	// A whole number in p_reg, as the `int` a 32-bit value takes: checked, never cut.
	uint32_t as_int32(uint32_t p_reg, uint32_t p_position);

	// A class Godot keeps one object of (Input): tagged [Singleton] in the declarations.
	static bool is_singleton(const Symbol *p_class);
	static bool on_object(const Expr *p_member);
	bool stored_element(const Expr *p_expression);

	// The engine's one object of a singleton class. Everything written `Input.x` is then a member of it.
	uint32_t singleton(const Symbol *p_class, uint32_t p_position);

	// A property or field of an engine object.
	Place engine_place(Symbol *p_symbol, Expr *p_object, uint32_t p_position, Expr *p_later = nullptr);

	// An operand for the engine, as the checker typed it: Godot tells a whole number from a fraction
	// (`Vector2i * 2` is a Vector2i), and the interpreter's own arithmetic is all in fractions.
	uint32_t operand(Expr *p_expression, const Type *p_beside = nullptr);

	// An operator that Godot carries out, because an operand is one of its values. The operator goes as
	// AS3 writes it; `v += w` is `v + w`.
	uint32_t engine_operate(TokenKind p_op, std::vector<uint32_t> p_operands, Kind p_result, const Type *p_type, uint32_t p_position);
};

} // namespace ir
} // namespace kas3
