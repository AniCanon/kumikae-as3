// Calls of Godot's methods and functions, the arguments the engine is given, and signals reached by name
// (lower_engine_calls.cpp): a helper the lowerer makes where it needs one, with the lowerer's own state.
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

class EngineCalls {
	Lowerer &l_;

	// An instruction a call still needs once its arguments are worked out (the signal it is made on).
	struct Later {
		Op op;
		uint32_t position, a, b, c;
	};
	uint32_t engine_call(Call *p_call, Symbol *p_method, uint32_t p_receiver = NONE, std::optional<Later> p_before = std::nullopt);

	// A method of Godot's that is called on no object: `Vector2.fromAngle(a)`.
	uint32_t static_call(Call *p_call, Symbol *p_method);

	// A method written where Godot takes a function (`onHit`, `this.onHit`, `other.onHit`): the method
	// bound to its object. A private method is that very function; any other is the object's own override,
	// found when the line runs.
	uint32_t bound_method(Expr *p_expression);

	std::optional<std::string> defaults_key(const Symbol *p_method) const;
	// The value of an argument whose default is one AS3 can't write, Godot's own when it is null.
	uint32_t godot_default_argument(Expr *p_argument, const Type *p_type, const std::string &p_default);

public:
	explicit EngineCalls(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	std::optional<uint32_t> signal_call_by_name(Call *p_call);
	uint32_t engine_text(Call *p_call);
	uint32_t godot_call(Call *p_call, Symbol *p_method);
	uint32_t global_call(Call *p_call, Symbol *p_function, Kind p_want);

	// `object[name]` where a signal's member follows: the signal, by its AS3 name. NONE if this is not one.
	// The object and the name are worked out here; the register is filled by a HostMember that the caller
	// emits once the call's arguments are worked out, so that a wrong name fails after them, as a wrong
	// member does in AS3.
	uint32_t signal_by_name(Expr *p_expression, Expr *p_later, uint32_t *r_object, uint32_t *r_name);

	// Arguments for the engine: each as its parameter says, or as it comes where the parameter takes anything.
	// p_defaults: "Class.method" in Godot's names, for a method whose defaults Godot's description gives.
	void engine_arguments(FunctionDecl *p_function, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list,
			std::optional<std::string_view> p_defaults = std::nullopt);
};

} // namespace ir
} // namespace kas3
