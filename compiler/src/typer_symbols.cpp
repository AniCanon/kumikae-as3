#include "typer.h"

namespace kas3 {

const Type *Typer::symbol_type(Symbol *p_symbol, const Type *p_element) {
	if (!p_symbol) {
		return unknown_;
	}
	switch (p_symbol->kind) {
		case SymbolKind::Class:
		case SymbolKind::Interface:
			return compilation_.class_object_type(compilation_.instance_type(p_symbol));
		case SymbolKind::Function:
		case SymbolKind::Method:
		case SymbolKind::LocalFunction:
			return function_;
		case SymbolKind::Parameter:
			return p_symbol->is_rest ? array_ : compilation_.type_of(p_symbol->type, p_element);
		case SymbolKind::Variable:
		case SymbolKind::Field:
		case SymbolKind::Local:
		case SymbolKind::CatchVariable:
			return compilation_.type_of(p_symbol->type, p_element);
		case SymbolKind::Property:
			if (p_symbol->getter) {
				return compilation_.type_of(p_symbol->getter->return_type, p_element);
			}
			if (p_symbol->setter && p_symbol->setter->parameters.count > 0) {
				return compilation_.type_of(p_symbol->setter->parameters[0].type, p_element);
			}
			return unknown_;
		case SymbolKind::Arguments:
			return any_;
	}
	return unknown_;
}

bool Typer::derives_from(Symbol *p_class, Symbol *p_base) const {
	for (Symbol *owner = p_class; owner; owner = owner->klass->base) {
		if (owner == p_base) {
			return true;
		}
	}
	return false;
}

bool Typer::accessible(Symbol *p_member, uint32_t p_offset) {
	Symbol *owner = p_member->owner;
	if (p_member->modifiers & Modifiers::Public) {
		return true;
	}
	if (p_member->modifiers & Modifiers::Private) {
		if (class_ == owner) {
			return true;
		}
		error(p_offset, quoted(p_member->name) + " is private in " + std::string(owner->name));
		return false;
	}
	if (p_member->modifiers & Modifiers::Protected) {
		if (class_ && derives_from(class_, owner)) {
			return true;
		}
		error(p_offset, quoted(p_member->name) + " is protected in " + std::string(owner->name));
		return false;
	}
	if (owner->package == unit_.package) {
		return true;
	}
	error(p_offset, quoted(p_member->name) + " in " + std::string(owner->name) + " is not visible from this package");
	return false;
}

Symbol *Typer::instance_member(Symbol *p_class, std::string_view p_name) {
	if (p_class->kind == SymbolKind::Class) {
		for (Symbol *owner = p_class; owner; owner = owner->klass->base) {
			if (Symbol *member = find_in(owner->klass->members, p_name)) {
				return member;
			}
		}
		// None. A static member of that name is returned so that the error can say so.
		for (Symbol *owner = p_class; owner; owner = owner->klass->base) {
			if (Symbol *member = find_in(owner->klass->statics, p_name)) {
				return member;
			}
		}
		return nullptr;
	}
	// An interface: itself, everything it extends, then what every object has.
	std::vector<Symbol *> pending{ p_class };
	for (size_t i = 0; i < pending.size(); i++) {
		if (Symbol *member = find_in(pending[i]->klass->members, p_name)) {
			return member;
		}
		for (Symbol *parent : pending[i]->klass->interfaces) {
			if (parent && std::find(pending.begin(), pending.end(), parent) == pending.end()) {
				pending.push_back(parent);
			}
		}
	}
	Symbol *object = compilation_.object_class();
	return object ? find_in(object->klass->members, p_name) : nullptr;
}

bool Typer::members_known(Symbol *p_class) {
	if (p_class->kind == SymbolKind::Class) {
		return !compilation_.hierarchy_broken(p_class);
	}
	std::vector<Symbol *> pending{ p_class };
	for (size_t i = 0; i < pending.size(); i++) {
		if (pending[i]->klass->broken) {
			return false;
		}
		auto &parents = pending[i]->klass->interfaces;
		InterfaceDecl *decl = static_cast<InterfaceDecl *>(pending[i]->node);
		if (parents.size() != decl->extends.count) {
			return false;
		}
		for (Symbol *parent : parents) {
			if (!parent) {
				return false; // One that did not resolve.
			}
			if (std::find(pending.begin(), pending.end(), parent) == pending.end()) {
				pending.push_back(parent);
			}
		}
	}
	return true;
}

const Type *Typer::member_type(const Type *p_object, Member *p_member) {
	switch (p_object->kind) {
		case Type::Any:
			return any_;
		case Type::Unknown:
		case Type::Void:
		case Type::Null:
			return unknown_;
		case Type::ClassObject: {
			const Type *instance = p_object->element;
			Symbol *owner = instance->kind == Type::Vector ? compilation_.vector_class() : instance->symbol;
			if (!owner) {
				return unknown_;
			}
			Symbol *member = find_in(owner->klass->statics, p_member->name);
			if (!member) {
				if (find_in(owner->klass->members, p_member->name)) {
					error(p_member->name_offset, quoted(p_member->name) + " is an instance member of " +
														 std::string(owner->name) + "; it needs an object");
				} else {
					error(p_member->name_offset,
							quoted(owner->name) + " has no static member " + quoted(p_member->name));
				}
				return unknown_;
			}
			if (!accessible(member, p_member->name_offset)) {
				return unknown_;
			}
			p_member->member = member;
			return symbol_type(member, nullptr);
		}
		case Type::Instance:
		case Type::Vector: {
			bool vector = p_object->kind == Type::Vector;
			Symbol *owner = vector ? compilation_.vector_class() : p_object->symbol;
			if (!owner) {
				return unknown_;
			}
			Symbol *member = instance_member(owner, p_member->name);
			if (!member) {
				if (owner->modifiers & Modifiers::Dynamic) {
					return any_; // A dynamic class takes any property. Its subclasses do not.
				}
				if (members_known(owner)) {
					error(p_member->name_offset, quoted(owner->name) + " has no member " + quoted(p_member->name));
				}
				return unknown_;
			}
			// A Godot singleton's members are declared static (`Input.isActionPressed`), and its one object
			// has them too: `var i:Input = Input.singleton; i.isActionPressed(...)`.
			if (member->is_static() && !(member->owner && has_tag(member->owner, "Singleton"))) {
				error(p_member->name_offset, quoted(p_member->name) + " is a static member of " +
													 std::string(member->owner->name) + "; write " +
													 std::string(member->owner->name) + "." + std::string(p_member->name));
				return unknown_;
			}
			if (!accessible(member, p_member->name_offset)) {
				return unknown_;
			}
			p_member->member = member;
			return symbol_type(member, vector ? p_object->element : nullptr);
		}
	}
	return unknown_;
}

} // namespace kas3
