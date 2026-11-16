// Operators on two values: arithmetic, bitwise, string, logical, comparisons, equality, `is` and `as`
// (lower_operators.cpp; comparisons and equality, lower_comparisons.cpp; on untyped values, lower_any.cpp). A
// helper the lowerer makes where it needs one, with the lowerer's own state.
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

class Operators {
	Lowerer &l_;

	uint32_t godot_object_of(uint32_t p_object, Expr *p_written, uint32_t p_position);
	uint32_t any_equality(Binary *p_binary);
	uint32_t any_comparison(Binary *p_binary);
	uint32_t any_add(Binary *p_binary);
	std::optional<uint32_t> any_type_test(Binary *p_binary);
	uint32_t wide_order(Expr *p_left, Expr *p_right, uint32_t p_position);
	uint32_t number_zero(uint32_t p_position);
	uint32_t engine_against_null(Binary *p_binary, Expr *p_value, Kind p_kind);
	uint32_t wide_against_other(Binary *p_binary);
	uint32_t wide_equality(Binary *p_binary);
	uint32_t concatenate(uint32_t p_left, uint32_t p_right, uint32_t p_position);

	uint32_t logical(Binary *p_binary, Kind p_want);
	uint32_t type_test(Binary *p_binary);
	uint32_t instance_of(Binary *p_binary);
	uint32_t untyped_godot_test(Binary *p_binary, uint32_t p_godot_class);
	uint32_t godot_test(Binary *p_binary, uint32_t p_godot_class);
	uint32_t comparison(Binary *p_binary);
	uint32_t arithmetic(Binary *p_binary, Kind p_want);

public:
	explicit Operators(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	// An arithmetic, bitwise or string operation on two values already evaluated.
	uint32_t operate(TokenKind p_op, uint32_t p_left, uint32_t p_right, bool p_string, uint32_t p_position);

	uint32_t binary(Binary *p_binary, Kind p_want);
	uint32_t equality(Binary *p_binary);
};

} // namespace ir
} // namespace kas3
