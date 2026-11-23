#include "typer.h"

#include "held_in_place.h"

namespace kas3 {

void Typer::list(const List<Expr *> &p_list) {
	for (Expr *item : p_list) {
		expression(item);
	}
}

std::string Typer::counted(size_t p_count, const char *p_noun) {
	return std::to_string(p_count) + " " + p_noun + (p_count == 1 ? "" : "s");
}

void Typer::arguments(FunctionDecl *p_function, std::string_view p_name, const List<Expr *> &p_arguments, uint32_t p_offset, const Type *p_element) {
	size_t required = 0;
	size_t most = 0;
	bool rest = false;
	for (const Parameter &parameter : p_function->parameters) {
		if (parameter.is_rest) {
			rest = true;
			break;
		}
		most++;
		if (!parameter.default_value) {
			required = most;
		}
	}
	size_t given = p_arguments.count;
	if (given < required || (given > most && !rest)) {
		std::string takes;
		if (rest || (required < most && given < required)) {
			takes = "at least " + counted(required, "argument");
		} else if (required < most) {
			takes = "at most " + counted(most, "argument");
		} else {
			takes = most == 0 ? "no arguments" : counted(most, "argument");
		}
		error(p_offset, quoted(p_name) + " takes " + takes + " but " + std::to_string(given) +
								(given == 1 ? " was" : " were") + " given");
		return;
	}
	for (size_t i = 0; i < given && i < most; i++) {
		store(p_arguments[i], compilation_.type_of(p_function->parameters[i].type, p_element));
	}
	// A vector's method given elements: one declared to take the element's type (insertAt), and the values
	// push(), unshift() and splice() take after the others.
	const bool adds = p_name == "push" || p_name == "unshift" || p_name == "splice";
	for (size_t i = 0; p_element && i < given; i++) {
		if (i < most ? compilation_.type_of(p_function->parameters[i].type, p_element) == p_element : rest && adds) {
			no_null_place(p_arguments[i], p_element);
		}
	}
}

void Typer::no_null_place(const Expr *p_value, const Type *p_element) {
	if (p_value && p_value->type && p_value->type->kind == Type::Null && held_in_place(p_element)) {
		error(p_value->offset, "null can't be stored in a " + quoted(compilation_.type_name(compilation_.vector_type(p_element))) +
									   ": each of its places holds a " + quoted(compilation_.type_name(p_element)) + " itself");
	}
}

bool Typer::abstract(const Type *p_instance) {
	return p_instance->kind == Type::Instance && has_tag(p_instance->symbol, "Abstract");
}

void Typer::construct(const Type *p_instance, const List<Expr *> &p_arguments, uint32_t p_offset) {
	bool vector = p_instance->kind == Type::Vector;
	Symbol *owner = vector ? compilation_.vector_class() : p_instance->symbol;
	if (!owner || owner->kind != SymbolKind::Class) {
		return;
	}
	if (Symbol *constructor = owner->klass->constructor) {
		arguments(static_cast<FunctionDecl *>(constructor->node), owner->name, p_arguments, p_offset,
				vector ? p_instance->element : nullptr);
	} else if (!owner->unit->is_declaration && p_arguments.count > 0) {
		// A class that declares no constructor takes no arguments. One in a declaration file may
		// simply not say.
		error(p_offset, quoted(owner->name) + " takes no arguments but " + std::to_string(p_arguments.count) +
								(p_arguments.count == 1 ? " was" : " were") + " given");
	}
}

uint32_t Typer::name_offset(const Expr *p_callee) {
	return p_callee->kind == NodeKind::Member ? static_cast<const Member *>(p_callee)->name_offset : p_callee->offset;
}

const Type *Typer::call(Call *p_call) {
	list(p_call->arguments);
	Expr *callee = p_call->callee;
	if (!callee) {
		return unknown_;
	}
	if (callee->kind == NodeKind::SuperExpr) {
		callee->type = unknown_;
		if (class_ && !static_ && class_->klass->base && !compilation_.hierarchy_broken(class_)) {
			construct(compilation_.instance_type(class_->klass->base), p_call->arguments, callee->offset);
		}
		return compilation_.void_type(); // `super(...)` in a constructor.
	}
	const Type *type = expression(callee);
	Symbol *symbol = nullptr;
	const Type *element = nullptr;
	if (callee->kind == NodeKind::Name) {
		symbol = static_cast<Name *>(callee)->symbol;
	} else if (callee->kind == NodeKind::Member) {
		Member *member = static_cast<Member *>(callee);
		symbol = member->resolved();
		if (member->member && member->object && member->object->type && member->object->type->kind == Type::Vector) {
			element = member->object->type->element;
		}
	}
	if (symbol && symbol->kind == SymbolKind::Method && callee->kind == NodeKind::Member && symbol->owner == compilation_.builtin("Object") &&
			static_cast<Member *>(callee)->object->type == compilation_.instance_type(symbol->owner)) {
		// One of Object's own methods on a value typed `Object`: what runs is the method of what the value
		// is by then (a Number's toString takes a radix), so the arguments are not counted here, as
		// Adobe's compiler does not count them.
		return compilation_.type_of(static_cast<FunctionDecl *>(symbol->node)->return_type);
	}
	if (symbol && (symbol->kind == SymbolKind::Method || symbol->kind == SymbolKind::Function ||
						  symbol->kind == SymbolKind::LocalFunction)) {
		FunctionDecl *function = static_cast<FunctionDecl *>(symbol->node);
		arguments(function, symbol->name, p_call->arguments, name_offset(callee), element);
		return compilation_.type_of(function->return_type, element);
	}
	switch (type->kind) {
		case Type::Callback:
			if (type->parameters.size() != p_call->arguments.count) {
				error(callee->offset, "callback takes " + counted(type->parameters.size(), "argument") +
						" but received " + std::to_string(p_call->arguments.count));
			} else {
				for (size_t i = 0; i < type->parameters.size(); ++i) {
					store(p_call->arguments[i], type->parameters[i]);
				}
			}
			return type->element;
		case Type::ClassObject:
			if (type->element && type->element->kind == Type::Instance && type->element->symbol == compilation_.builtin("Date")) {
				return string_; // Date(x) is the time now as text, as in Flash: no Date.
			}
			return type->element; // `int(x)`, `Player(x)`: a conversion.
		case Type::Any:
			return any_;
		case Type::Instance:
			return dynamic_value(type) ? any_ : unknown_;
		default:
			return unknown_;
	}
}

bool Typer::dynamic_value(const Type *p_type) const {
	return p_type == function_ || p_type == object_ || p_type == class_type_;
}

} // namespace kas3
