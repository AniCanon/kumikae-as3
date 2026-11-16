// Calls (lower_calls.cpp): what a call is (a method or a function of the program, a conversion, `trace`,
// `super(...)`, or one that another part of the lowering takes: Godot's, the library's, through a value),
// and a call's arguments as the function's parameters expect them. A helper the lowerer makes where it
// needs one, with the lowerer's own state.
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

class Calls {
	Lowerer &l_;

	uint32_t super_call(Call *p_call);
	uint32_t cast(Call *p_call, Symbol *p_type, Kind p_want);
	uint32_t trace_call(Call *p_call);
	uint32_t script_call(Call *p_call, Symbol *p_symbol, uint32_t p_function);
	uint32_t symbol_call(Call *p_call, Symbol *p_symbol, Kind p_want);
	uint32_t written_value(Expr *p_value, Kind p_conversion, uint32_t p_position);

public:
	explicit Calls(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	uint32_t call(Call *p_call, Kind p_want);

	// The arguments of a call as the function's parameters expect them, defaults filled in.
	void call_arguments(FunctionDecl *p_function, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list);

	uint32_t call_result(Symbol *p_function, uint32_t p_position, Kind *r_kind);

	void constructor_arguments(Symbol *p_class, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list);
};

} // namespace ir
} // namespace kas3
