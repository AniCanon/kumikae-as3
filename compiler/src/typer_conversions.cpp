#include "typer.h"

#include "casts.h"

namespace kas3 {

bool Typer::primitive(const Type *p_type) const {
	return numeric(p_type) || p_type == boolean_ || p_type == string_ || p_type->kind == Type::Any ||
			p_type->kind == Type::Null;
}

const Type *Typer::join(const Type *p_a, const Type *p_b) const {
	if (p_a == p_b) {
		return p_a;
	}
	if (p_a->kind == Type::Unknown || p_b->kind == Type::Unknown) {
		return unknown_;
	}
	if (numeric(p_a) && numeric(p_b)) {
		// An Int64 and another whole number: the Int64 holds both.
		return (p_a == int64_ || p_b == int64_) && integral(p_a) && integral(p_b) ? int64_ : number_;
	}
	auto nullable = [&](const Type *p_type) {
		return (p_type->kind == Type::Instance && !numeric(p_type) && p_type != boolean_) ||
				p_type->typed_sequence() || p_type->kind == Type::Callback || p_type->kind == Type::ClassObject;
	};
	if (p_a->kind == Type::Null && nullable(p_b)) {
		return p_b;
	}
	if (p_b->kind == Type::Null && nullable(p_a)) {
		return p_a;
	}
	return any_;
}

bool Typer::is_a(Symbol *p_class, Symbol *p_target, bool *r_unsure) const {
	// Asked again and again for the same pair, and a hierarchy can be deep.
	auto known = related_.find({ p_class, p_target });
	if (known != related_.end()) {
		*r_unsure |= known->second.second;
		return known->second.first;
	}
	bool unsure = false;
	bool result = walk_is_a(p_class, p_target, &unsure);
	related_[{ p_class, p_target }] = { result, unsure };
	*r_unsure |= unsure;
	return result;
}

bool Typer::walk_is_a(Symbol *p_class, Symbol *p_target, bool *r_unsure) const {
	std::vector<Symbol *> pending;
	std::unordered_set<Symbol *> seen;
	for (Symbol *owner = p_class; owner; owner = owner->klass->base) {
		if (owner->klass->broken || owner->klass->incomplete) {
			*r_unsure = true;
		}
		pending.push_back(owner);
		if (owner->kind != SymbolKind::Class) {
			break;
		}
	}
	for (size_t i = 0; i < pending.size(); i++) {
		if (pending[i] == p_target) {
			return true;
		}
		if (pending[i]->klass->broken) {
			*r_unsure = true;
		}
		for (Symbol *parent : pending[i]->klass->interfaces) {
			if (!parent) {
				*r_unsure = true; // One that did not resolve.
			} else if (seen.insert(parent).second) {
				pending.push_back(parent);
			}
		}
	}
	return false;
}

bool Typer::unresolved(const Type *p_type) {
	for (; p_type; p_type = p_type->element) {
		if (p_type->kind == Type::Unknown) {
			return true;
		}
	}
	return false;
}

bool Typer::converts_to_engine(const Type *p_from, const Type *p_to) const {
	auto in_godot = [&](const Type *p_type) {
		return godot_ && p_type->kind == Type::Instance && p_type->symbol && p_type->symbol->package == godot_;
	};
	if (!in_godot(p_to)) {
		return false;
	}
	std::string_view name = p_to->symbol->name;
	if (name == "GodotArray") {
		return p_from == array_ || p_from->typed_sequence();
	}
	if (name == "GodotDictionary") {
		return p_from == object_ || p_from == dictionary_;
	}
	const std::string_view head = "Packed";
	const std::string_view tail = "Array";
	if (name.size() <= head.size() + tail.size() || name.substr(0, head.size()) != head ||
			name.substr(name.size() - tail.size()) != tail) {
		return false;
	}
	if (p_from == array_) {
		return true; // Its elements are checked when it is converted.
	}
	if (!p_from->typed_sequence()) {
		return false;
	}
	std::string_view element = name.substr(head.size(), name.size() - head.size() - tail.size());
	const Type *from = p_from->element;
	if (element == "Byte" || element == "Int32" || element == "Int64" || element == "Float32" ||
			element == "Float64") {
		return numeric(from);
	}
	if (element == "String") {
		return from == string_;
	}
	return in_godot(from) && from->symbol->name == element; // Vector2, Vector3, Vector4, Color.
}

auto Typer::fits(const Type *p_from, const Type *p_to) const -> Typer::Fit {
	if (p_from == p_to || p_from->kind == Type::Any || p_to->kind == Type::Any || p_to->kind == Type::Void ||
			unresolved(p_from) || unresolved(p_to)) {
		return Fit::Yes;
	}
	if (p_from->kind == Type::Void) {
		return Fit::No;
	}
	// Anything converts to a Boolean, everything is an Object, and the number types convert to each
	// other. Adobe's compiler only warns when null is stored in a number, so nothing is said here.
	if (p_to == boolean_ || p_to == object_ || (numeric(p_from) && numeric(p_to)) || p_from->kind == Type::Null) {
		return Fit::Yes;
	}
	if (converts_to_engine(p_from, p_to)) {
		return Fit::Yes;
	}
	if (p_from->kind == Type::ClassObject) {
		return p_to == class_type_ ? Fit::Yes : Fit::No;
	}
	if (p_to->kind == Type::ClassObject || p_to->kind == Type::Null) {
		return Fit::Yes; // Not a type that can be written.
	}
	if (p_from->typed_sequence() || p_to->typed_sequence()) {
		if (p_from->typed_sequence() && p_to->typed_sequence()) {
			// Vectors of different element types are unrelated, even when the elements are related.
			return Fit::No;
		}
		return p_from == object_ ? Fit::Cast : Fit::No;
	}
	Symbol *from = p_from->symbol;
	Symbol *to = p_to->symbol;
	if (!from || !to) {
		return Fit::Yes;
	}
	bool unsure = false;
	if (is_a(from, to, &unsure)) {
		return Fit::Yes;
	}
	bool narrower = p_from == object_ || is_a(to, from, &unsure);
	if (unsure) {
		return Fit::Yes;
	}
	return narrower ? Fit::Cast : Fit::No;
}

bool Typer::callback_fits(Expr *p_value, const Type *p_to) {
	if (p_value->type == p_to || p_value->type->kind == Type::Null) {
		return true;
	}
	FunctionDecl *decl = nullptr;
	if (p_value->kind == NodeKind::FunctionExpr) {
		decl = static_cast<FunctionExpr *>(p_value)->function;
	} else {
		Symbol *symbol = p_value->kind == NodeKind::Name ? static_cast<Name *>(p_value)->symbol :
				p_value->kind == NodeKind::Member ? static_cast<Member *>(p_value)->resolved() : nullptr;
		if (symbol && (symbol->kind == SymbolKind::Function || symbol->kind == SymbolKind::Method)) {
			decl = static_cast<FunctionDecl *>(symbol->node);
		}
	}
	bool matches = decl && decl->parameters.count == p_to->parameters.size() && compilation_.type_of(decl->return_type) == p_to->element;
	for (size_t i = 0; matches && i < p_to->parameters.size(); ++i) {
		const Parameter &param = decl->parameters[i];
		matches = !param.is_rest && !param.default_value && compilation_.type_of(param.type) == p_to->parameters[i];
	}
	return matches;
}

void Typer::store(Expr *p_value, const Type *p_to) {
	// Which type Adobe's compiler gives `a = b` used as a value is not known, so it is not judged.
	if (!p_value || !p_value->type || !p_to) {
		return;
	}
	if (p_value->kind == NodeKind::Assign && p_to->kind != Type::Callback &&
			!p_to->contains_fixed_array() && !p_value->type->contains_fixed_array()) {
		return;
	}
	if (p_to->kind == Type::Callback) {
		if (!callback_fits(p_value, p_to)) {
			error(p_value->offset, "callback requires the exact signature " + quoted(compilation_.type_name(p_to)));
		}
		return;
	}
	if (p_to == int64_) {
		// A whole number written out is taken digit by digit, so it must be one an Int64 can hold.
		const WideLiteral literal = read_wide_literal(p_value);
		if (literal.state == WideLiteral::State::TooLarge) {
			error(p_value->offset, std::string(literal.negative ? "-" : "") + std::string(literal.digits) + " does not fit an Int64");
			return;
		}
	} else if (p_to == number_ || p_to->kind == Type::Any || p_to == object_) {
		// Stored as a Number, as Flash does: one that a Number can't hold exactly is rounded, which is said.
		const WideLiteral literal = read_wide_literal(p_value);
		const uint64_t magnitude = literal.negative ? 0 - (uint64_t)literal.value : (uint64_t)literal.value;
		const bool exact = literal.state == WideLiteral::State::Fits && (uint64_t)(double)magnitude == magnitude;
		if (literal.state != WideLiteral::State::NotWhole && !exact) {
			const std::string written = std::string(literal.negative ? "-" : "") + std::string(literal.digits);
			warning(p_value->offset, written + " is rounded: a Number can't hold every digit of it. Store it in an Int64 to keep them");
		}
	}
	switch (fits(p_value->type, p_to)) {
		case Fit::Yes:
			break;
		case Fit::Cast: {
			// Only a type that casts or converts when it is called is advised that way.
			const bool casts = p_to->kind == Type::Vector || (p_to->kind == Type::Instance && class_call(p_to->symbol) != ClassCall::Other);
			error(p_value->offset, "a value of type " + quoted(compilation_.type_name(p_value->type)) +
										   " may not be a " + quoted(compilation_.type_name(p_to)) +
										   (casts ? "; use 'as' or a cast" : "; use 'as'"));
			break;
		}
		case Fit::No:
			error(p_value->offset, "can't use a value of type " + quoted(compilation_.type_name(p_value->type)) +
										   " where " + quoted(compilation_.type_name(p_to)) + " is expected");
			break;
	}
}

void Typer::written(Expr *p_target) {
	if (!p_target) {
		return;
	}
	Symbol *symbol = nullptr;
	uint32_t offset = p_target->offset;
	bool own_object = true; // Named bare or through `this`.
	if (p_target->kind == NodeKind::Name) {
		symbol = static_cast<Name *>(p_target)->symbol;
	} else if (p_target->kind == NodeKind::Member) {
		Member *member = static_cast<Member *>(p_target);
		if (member->object && member->object->type && member->object->type->kind == Type::FixedArray && member->name == "length") {
			error(member->name_offset, "'length' is read-only");
			return;
		}
		symbol = member->resolved();
		offset = member->name_offset;
		own_object = member->object && member->object->kind == NodeKind::ThisExpr;
	}
	if (!symbol) {
		return;
	}
	std::string name = quoted(symbol->name);
	switch (symbol->kind) {
		case SymbolKind::Field:
		case SymbolKind::Variable:
		case SymbolKind::Local: {
			if (!symbol->is_const) {
				break;
			}
			// An instance constant gets its value in the constructor of its own class.
			bool constructing = symbol->kind == SymbolKind::Field && !symbol->is_static() && own_object &&
					closures_ == 0 && class_ == symbol->owner && class_->klass->constructor &&
					class_->klass->constructor->node == current_;
			if (!constructing) {
				error(offset, "can't assign to the constant " + name);
			}
		} break;
		case SymbolKind::Property: {
			// The setter may be in a base class or an interface that did not resolve.
			Symbol *through = nullptr;
			property_of(p_target, &through, &offset);
			bool known = false;
			if (!compilation_.property_half(through, symbol, FunctionDecl::Setter, &known) && known) {
				error(offset, name + " is read-only");
			}
		} break;
		case SymbolKind::Method:
			// A built-in class's methods may be on its prototype, where they can be replaced.
			if (!symbol->unit->is_builtin) {
				error(offset, "can't assign to the method " + name);
			}
			break;
		case SymbolKind::Function:
			error(offset, "can't assign to the function " + name);
			break;
		case SymbolKind::Class:
		case SymbolKind::Interface:
			error(offset, "can't assign to the class " + name);
			break;
		default:
			break;
	}
}

Symbol *Typer::property_of(Expr *p_expression, Symbol **r_through, uint32_t *r_offset) {
	Symbol *symbol = nullptr;
	*r_through = nullptr;
	*r_offset = p_expression->offset;
	if (p_expression->kind == NodeKind::Name) {
		symbol = static_cast<Name *>(p_expression)->symbol;
	} else if (p_expression->kind == NodeKind::Member) {
		Member *member = static_cast<Member *>(p_expression);
		symbol = member->resolved();
		*r_offset = member->name_offset;
		const Type *object = member->object ? member->object->type : nullptr;
		*r_through = object && object->kind == Type::Instance ? object->symbol : nullptr;
	}
	return symbol && symbol->kind == SymbolKind::Property ? symbol : nullptr;
}

void Typer::read(Expr *p_expression) {
	Symbol *through = nullptr;
	uint32_t offset = 0;
	Symbol *property = p_expression == assigned_ ? nullptr : property_of(p_expression, &through, &offset);
	bool known = false;
	if (property && !compilation_.property_half(through, property, FunctionDecl::Getter, &known) && known) {
		error(offset, quoted(property->name) + " is write-only");
	}
}

} // namespace kas3
