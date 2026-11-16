// Getters, setters and interfaces (lower_members.cpp): a helper the lowerer makes where it needs one, with
// the lowerer's own state.
#pragma once

#include "lowerer.h"

namespace kas3 {
namespace ir {

class Members {
	Lowerer &l_;

	// How a half of the property is reached from the place: a function, a table slot or an interface method.
	uint32_t half(const FunctionDecl *p_function, Place::Dispatch p_dispatch);
	void call(const Place &p_place, uint32_t p_function, uint32_t p_out, uint32_t p_value, uint32_t p_position);
	// The number of a method's name among the interfaces' methods, given on first use.
	uint32_t method_number(const std::string &p_name);
	// The interface, if it is one of the program's, and what it extends, each once.
	void add_interface(const Symbol *p_interface, std::vector<const Symbol *> &r_list) const;
	// An interface method's number; NONE, refused, if it can't be reached; nothing for any other symbol.
	std::optional<uint32_t> interface_method(Symbol *p_method, Expr *p_callee);

public:
	explicit Members(Lowerer &p_lowerer) :
			l_(p_lowerer) {}

	// A getter or a setter of a class: its function, "Class/get x", and for an instance one the slot of
	// that name in the method table.
	void declare_accessor(uint32_t p_class, FunctionDecl *p_function);

	// A property of the program's own, named bare (p_object null) or through an object, as a place. The
	// object is worked out here, once. Nothing if the name is not such a property.
	std::optional<Place> property(Expr *p_target, Expr *p_object, Expr *p_later);

	uint32_t get(const Place &p_place, uint32_t p_position);
	void set(const Place &p_place, uint32_t p_value, uint32_t p_position);

	// Every interface of the program gets its place among the classes, before any class is declared: a
	// field or a parameter may be of one. Each of its methods gets the number of its name.
	void reserve_interfaces(const std::vector<Unit *> &p_units);
	// Once the class's methods are declared: the interfaces it implements, and the function it runs for
	// each of their methods, found by name as AVM2 finds it, an inherited one or an override included.
	void implement(uint32_t p_class, const Symbol *p_symbol);
	// `shape.area(2)`, through a value typed as an interface. Nothing if the method is not an interface's.
	std::optional<uint32_t> interface_call(Call *p_call, Symbol *p_method);
	// `shape.area` as a function value: the object's own, bound to it.
	std::optional<uint32_t> interface_value(Expr *p_expression, Symbol *p_method);
};

} // namespace ir
} // namespace kas3
