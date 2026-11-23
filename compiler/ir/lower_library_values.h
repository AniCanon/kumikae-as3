// The library's own functions as values (lower_library_values.cpp): `Math.max`, `trace`, `parseInt`,
// `String.fromCharCode`. A helper the lowerer makes where it needs one, with the lowerer's own state.
#pragma once

#include "lowerer.h"

#include <functional>

namespace kas3 {
namespace ir {

class LibraryValues {
	Lowerer &l_;

	// The function's result, given back to the caller as the declaration says it is held.
	void give(uint32_t p_value, uint32_t p_position);
	// A call of the function with the first p_count parameters, as the same call written out is lowered.
	void call_with(const PendingFunction &p_pending, const std::vector<uint32_t> &p_registers, uint32_t p_count, uint32_t p_position);
	// The arguments past the declared ones, one after another.
	void each_rest(uint32_t p_rest, uint32_t p_position, const std::function<void(uint32_t)> &p_body);
	// The three the library declares with `...rest`, whose calls take as many arguments as are written.
	void extreme(const PendingFunction &p_pending, const std::vector<uint32_t> &p_registers, uint32_t p_position);
	void traced(uint32_t p_rest, uint32_t p_position);
	void characters(uint32_t p_rest, uint32_t p_position);

public:
	explicit LibraryValues(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	// Whether the symbol is a function of the library that is called on no value: a top-level function, or
	// a static method of one of its classes.
	static bool is_function(const Symbol *p_symbol);
	// `Math.max`, `trace`: a Function value of a function of the program that calls it.
	uint32_t function_value(Expr *p_expression, Symbol *p_function);
	// The body of that function (Functions::lower_pending_functions).
	void lower(const PendingFunction &p_pending);
};

} // namespace ir
} // namespace kas3
