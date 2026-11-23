// The body of each function (lower_bodies.cpp): its start and end, its parameters, a method, a constructor
// (with the fields' first values and the signals it declares), a class's static initializer, and package
// variables. A helper the lowerer makes where it needs one, with the lowerer's own state.
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

class Bodies {
	Lowerer &l_;

	static bool calls_super(Block *p_body);
	static bool names_arguments(Block *p_body);
	uint32_t own_signal(std::string_view p_name, uint32_t p_position);
	void first_values(ClassDecl *p_class, uint32_t p_index);

public:
	explicit Bodies(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	void begin(uint32_t p_function, Unit &p_unit, Symbol *p_class);

	// Parameters take the first registers; an instance method's first is `this`.
	void parameters(FunctionDecl *p_function);
	// The register of one parameter, of the kind it is held as, with what a caller's value is checked against.
	uint32_t parameter_register(const Parameter &p_parameter, Kind p_kind);

	void end(uint32_t p_position);

	void lower_function(Unit &p_unit, Symbol *p_class, FunctionDecl *p_decl);

	void lower_constructor(Unit &p_unit, ClassDecl *p_class);

	// A function that gives the class's static fields their values.
	void lower_statics(Unit &p_unit, ClassDecl *p_class);

	void lower_package_variables(Unit &p_unit, VarDecl *p_decl);
};

} // namespace ir
} // namespace kas3
