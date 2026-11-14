#include "typer.h"

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
}

bool Typer::abstract(const Type *p_instance) {
	Symbol *symbol = p_instance->kind == Type::Instance ? p_instance->symbol : nullptr;
	if (!symbol || symbol->kind != SymbolKind::Class || !symbol->unit || !symbol->unit->is_declaration ||
			!symbol->node || symbol->node->kind != NodeKind::ClassDecl) {
		return false;
	}
	for (const Metadata &tag : static_cast<ClassDecl *>(symbol->node)->metadata) {
		if (tag.name == "Abstract") {
			return true;
		}
	}
	return false;
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
		symbol = member->symbol ? member->symbol : member->member;
		if (member->member && member->object && member->object->type && member->object->type->kind == Type::Vector) {
			element = member->object->type->element;
		}
	}
	if (symbol && (symbol->kind == SymbolKind::Method || symbol->kind == SymbolKind::Function ||
						  symbol->kind == SymbolKind::LocalFunction)) {
		FunctionDecl *function = static_cast<FunctionDecl *>(symbol->node);
		arguments(function, symbol->name, p_call->arguments, name_offset(callee), element);
		return compilation_.type_of(function->return_type, element);
	}
	switch (type->kind) {
		case Type::ClassObject:
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
