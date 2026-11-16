// Each class's place in the module, before any body is lowered (lower_classes.cpp): what it inherits, its
// fields, its methods and their table slots, and Godot's virtuals it answers. A helper the lowerer makes
// where it needs one, with the lowerer's own state.
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

class Classes {
	Lowerer &l_;

	Class inherited_class(ClassDecl *p_decl);
	void declare_fields(uint32_t p_class, VarDecl *p_decl);
	void record_godot_virtual(uint32_t p_class, const Symbol *p_symbol, FunctionDecl *p_function, uint32_t p_made);
	void declare_method(uint32_t p_class, const Symbol *p_symbol, FunctionDecl *p_function);

public:
	explicit Classes(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	// Gives the class its place in the module, before any class is filled in: a field may be of a class
	// declared later.
	void reserve_class(ClassDecl *p_decl);
	uint32_t add_function(uint32_t p_class, const std::string &p_name, uint32_t p_position, bool p_static);
	void give_method_slot(uint32_t p_class, FunctionDecl *p_function, uint32_t p_made);
	void declare_class(Unit &p_unit, ClassDecl *p_decl);
};

} // namespace ir
} // namespace kas3
