#include "lower_engine.h"

namespace kas3 {
namespace ir {

uint32_t Engine::engine_receiver(Expr *p_object, uint32_t p_position, bool *r_of_value, uint32_t *r_script_object) {
	*r_of_value = false;
	if (!p_object || p_object->kind == NodeKind::ThisExpr || p_object->kind == NodeKind::SuperExpr) {
		uint32_t klass = 0;
		if (l_.received_env_ && p_object) {
			// `this` and `super` written in a function expression are not the method's object (AS3).
			l_.refuse(p_position, "'this' or 'super' inside a function expression");
			return l_.default_value(Kind::Ref, p_position);
		}
		if (l_.fn().is_static || !l_.class_ || !l_.class_index(l_.class_, &klass) || l_.module_.classes[klass].native_base.empty()) {
			l_.refuse(p_position, "this use of a Godot member");
			return l_.default_value(Kind::Ref, p_position);
		}
		const uint32_t script = l_.self(p_position); // In a function expression, the method's object.
		uint32_t owner = l_.reg(Kind::Ref);
		l_.emit(Op::GetField, p_position, owner, script, 0);
		return owner;
	}
	uint32_t object = l_.expression(p_object, Kind::Void);
	Kind kind = l_.kind_at(object);
	if (kind == Kind::Ref || kind == Kind::Val) {
		*r_of_value = kind == Kind::Val;
		return object;
	}
	// Another script object: its owner.
	uint32_t klass = 0;
	const Type *type = p_object->type;
	if (kind == Kind::Obj && type && type->kind == Type::Instance && l_.class_index(type->symbol, &klass) &&
			!l_.module_.classes[klass].native_base.empty()) {
		uint32_t owner = l_.reg(Kind::Ref);
		if (r_script_object) {
			// A call: the owner is fetched once the arguments are worked out (owner_of), so that a
			// missing script object is reported after them, as it is for one of AS3's own members.
			*r_script_object = object;
			return owner;
		}
		owner_of(object, owner, p_object->offset);
		return owner;
	}
	l_.refuse(p_position, "this use of a Godot member");
	return l_.default_value(Kind::Ref, p_position);
}

// The Godot object a script object is attached to, into p_owner. A missing script object is an error here.
void Engine::owner_of(uint32_t p_script_object, uint32_t p_owner, uint32_t p_position) {
	l_.emit(Op::NullCheck, p_position, p_script_object);
	l_.emit(Op::GetField, p_position, p_owner, p_script_object, 0);
}

bool value_has_parts(const Symbol *p_class) {
	bool parts = false;
	for (const auto &member : p_class->klass->members) {
		parts = parts || (member.second->kind == SymbolKind::Field && !member.second->is_static());
	}
	return parts;
}

uint32_t Engine::zero_value(TypeRef *p_written, uint32_t p_position, uint32_t p_into) {
	const Type *type = p_written ? l_.compilation_.type_of(p_written) : nullptr;
	Kind kind = Kind::Void;
	uint32_t name = 0;
	if (!type || type->kind != Type::Instance || !type->symbol || !type->symbol->klass || !l_.kind_of(type, &kind) || kind != Kind::Val ||
			!l_.engine_name(type->symbol, std::string_view(), &name)) {
		return NONE;
	}
	if (!value_has_parts(type->symbol)) {
		return NONE;
	}
	uint32_t out = p_into != NONE ? p_into : l_.engine_reg(Kind::Val, l_.module_.engine_names[name].name);
	l_.emit(Op::HostNew, p_position, out, name, l_.arguments({}));
	return out;
}

bool Engine::stores_int32(const Type *p_type) const {
	if (!p_type || p_type->kind != Type::Instance || !p_type->symbol || !p_type->symbol->klass || !Lowerer::is_godot(p_type->symbol) ||
			l_.is_godot_object(p_type->symbol)) {
		return false;
	}
	bool any = false;
	for (const auto &member : p_type->symbol->klass->members) {
		if (member.second->kind != SymbolKind::Field || member.second->is_static()) {
			continue;
		}
		const Type *part = member.second->type ? l_.compilation_.type_of(member.second->type) : nullptr;
		Kind kind = Kind::Void;
		if (!part || part == p_type || !((l_.kind_of(part, &kind) && kind == Kind::I32) || stores_int32(part))) {
			return false;
		}
		any = true;
	}
	return any;
}

uint32_t Engine::as_int32(uint32_t p_reg, uint32_t p_position) {
	Kind kind = l_.kind_at(p_reg);
	if (kind == Kind::I32 || !Lowerer::is_numeric(kind)) {
		return p_reg;
	}
	if (kind == Kind::F64) {
		// A whole number that came out as not a number is 0, as AS3's int() makes it; one past 32 bits
		// is still a RangeError.
		uint32_t wide = l_.reg(Kind::I64);
		l_.emit(Op::F64ClampI64, p_position, wide, p_reg);
		return l_.convert(wide, Kind::I32, p_position);
	}
	return l_.convert(l_.convert(p_reg, Kind::I64, p_position), Kind::I32, p_position);
}

bool Engine::is_singleton(const Symbol *p_class) {
	return has_tag(p_class, "Singleton");
}

// An element of one of Godot's collections that is itself stored somewhere (`points[0]`, `grid[1][2]`):
// changing a part of it is writing it back there.
bool Engine::stored_element(const Expr *p_expression) {
	if (!p_expression || p_expression->kind != NodeKind::Index) {
		return false;
	}
	const Expr *object = static_cast<const Index *>(p_expression)->object;
	Kind kind = Kind::Void;
	return l_.kind_of(object->type, &kind) && kind == Kind::Val &&
			(object->kind == NodeKind::Name || object->kind == NodeKind::Member || stored_element(object));
}

// A member reached through an object (`i.mouseMode`), not through the class (`Input.mouseMode`).
bool Engine::on_object(const Expr *p_member) {
	if (!p_member || p_member->kind != NodeKind::Member) {
		return false;
	}
	const Expr *object = static_cast<const Member *>(p_member)->object;
	return object && object->type && object->type->kind == Type::Instance;
}

uint32_t Engine::singleton(const Symbol *p_class, uint32_t p_position) {
	uint32_t name = 0;
	if (!l_.engine_name(p_class, std::string_view(), &name)) {
		l_.refuse(p_position, "this use of a Godot member");
		return l_.default_value(Kind::Ref, p_position);
	}
	uint32_t out = l_.reg(Kind::Ref);
	l_.emit(Op::HostSingleton, p_position, out, name);
	return out;
}

auto Engine::constant_place(Symbol *p_symbol, uint32_t p_position) -> Lowerer::Place {
	Place place;
	Kind kind = Kind::Void;
	const Type *type = p_symbol->type ? l_.compilation_.type_of(p_symbol->type) : nullptr;
	uint32_t name = 0;
	Expr *value = nullptr;
	if (p_symbol->node && p_symbol->node->kind == NodeKind::VarDecl) {
		for (const Declarator &declarator : static_cast<VarDecl *>(p_symbol->node)->declarators) {
			value = declarator.symbol == p_symbol ? declarator.initializer : value;
		}
	}
	if (p_symbol->kind != SymbolKind::Field || !type || !l_.kind_of(type, &kind) || kind == Kind::Void ||
			(!value && !l_.engine_name(p_symbol->owner, p_symbol->name, &name))) {
		l_.refuse(p_position, "'" + std::string(p_symbol->name) + "' of Godot's " + std::string(p_symbol->owner ? p_symbol->owner->name : "API"));
		return place;
	}
	place.where = Place::Register;
	place.kind = kind;
	place.constant = true;
	if (value) {
		place.index = l_.expression(value, kind);
	} else {
		place.index = l_.engine_reg(kind, type);
		l_.emit(Op::HostConstant, p_position, place.index, name);
	}
	return place;
}

// p_later is what is worked out after the place and before it is written: a local it changes is read first.
auto Engine::engine_place(Symbol *p_symbol, Expr *p_object, uint32_t p_position, Expr *p_later) -> Lowerer::Place {
	Place place;
	uint32_t name = 0;
	Kind kind = Kind::F64;
	bool of_singleton = p_symbol->is_static() && p_symbol->kind == SymbolKind::Property && is_singleton(p_symbol->owner);
	if (of_singleton && p_symbol->name == "singleton") {
		// The singleton itself.
		place.where = Place::Register;
		place.index = singleton(p_symbol->owner, p_position);
		place.kind = Kind::Ref;
		place.constant = true;
		return place;
	}
	if (p_symbol->is_static() && !of_singleton) {
		return constant_place(p_symbol, p_position);
	}
	if (!l_.engine_name(p_symbol->owner, p_symbol->name, &name)) {
		l_.refuse(p_position, "'" + std::string(p_symbol->name) + "' of Godot's " + std::string(p_symbol->owner ? p_symbol->owner->name : "API"));
		return place;
	}
	// A property's type is what its getter returns, or what its setter takes.
	TypeRef *type = p_symbol->type;
	if (p_symbol->kind == SymbolKind::Property) {
		type = p_symbol->getter ? p_symbol->getter->return_type
				: p_symbol->setter && p_symbol->setter->parameters.count ? p_symbol->setter->parameters[0].type
																		   : nullptr;
	}
	if (!type || !l_.kind_of(l_.compilation_.type_of(type), &kind) || kind == Kind::Void) {
		l_.refuse(p_position, "a Godot member of type '" + l_.compilation_.type_name(l_.compilation_.type_of(type)) + "'");
		return place;
	}
	place.where = Place::Engine;
	place.index = name;
	place.kind = kind;
	place.type = l_.compilation_.type_of(type);
	if (of_singleton && !(p_object && p_object->type && p_object->type->kind == Type::Instance)) {
		place.object = singleton(p_symbol->owner, p_position);
		return place;
	}
	Kind whole = Kind::Void;
	// The value stored in a variable, a property, or an element of a collection (`points[0].x`).
	if (p_object && (p_object->kind == NodeKind::Name || p_object->kind == NodeKind::Member || stored_element(p_object)) &&
			l_.kind_of(p_object->type, &whole) && whole == Kind::Val) {
		// A member of a value that is itself stored somewhere.
		place.of_value = true;
		place.part = &l_.parts_.emplace_back();
		place.part->whole = l_.place_of(p_object, p_later);
		l_.whole_of(place, p_position);
		return place;
	}
	place.object = engine_receiver(p_object, p_position, &place.of_value, &place.script_object);
	if (place.script_object != NONE) {
		place.script_object = l_.settled(place.script_object, p_later, p_position);
	} else {
		place.object = l_.settled(place.object, p_later, p_position);
	}
	return place;
}

uint32_t Engine::operand(Expr *p_expression, const Type *p_beside) {
	if (stores_int32(p_beside) && (l_.whole(p_expression))) {
		// Beside a value of 32-bit integers (`cell * n`): Godot would cut a wider one off.
		return as_int32(l_.expression(p_expression, Kind::Void), p_expression->offset);
	}
	Kind kind = Kind::Void;
	if (!l_.value_kind(p_expression, &kind) || !Lowerer::is_numeric(kind)) {
		kind = Kind::Void;
	} else if (kind == Kind::F64 && l_.whole(p_expression)) {
		// A whole number worked out as a Number: Godot's integer holds every one. One that came out as not
		// a number (`5 % 0`) goes as 0, as AS3's int() makes it.
		uint32_t out = l_.reg(Kind::I64);
		l_.emit(Op::F64ClampI64, p_expression->offset, out, l_.expression(p_expression, Kind::F64));
		return out;
	}
	return l_.expression(p_expression, kind);
}

uint32_t Engine::engine_operate(TokenKind p_op, std::vector<uint32_t> p_operands, Kind p_result, const Type *p_type, uint32_t p_position) {
	std::string_view spelling = token_kind_spelling(p_op);
	switch (p_op) {
		case TokenKind::StrictEqual:
			spelling = "==";
			break;
		case TokenKind::StrictNotEqual:
			spelling = "!=";
			break;
		default:
			// A compound assignment is spelled as its operator and '='.
			if (spelling.size() >= 2 && spelling.back() == '=' && p_op != TokenKind::Equal && p_op != TokenKind::NotEqual &&
					p_op != TokenKind::LessEqual && p_op != TokenKind::GreaterEqual) {
				spelling.remove_suffix(1);
			}
			break;
	}
	for (uint32_t &operand : p_operands) {
		if (l_.kind_at(operand) == Kind::Obj) {
			operand = l_.convert(operand, Kind::Ref, p_position); // `node == this`: the Godot object it is.
		}
	}
	// Godot's description names a unary minus or plus "unary-" and "unary+".
	const bool unary = p_operands.size() == 1 && (spelling == "-" || spelling == "+");
	const std::optional<uint32_t> op = l_.engine_operator(unary ? "unary" + std::string(spelling) : std::string(spelling));
	if (!op) {
		l_.refuse(p_position, "the operator '" + std::string(spelling) + "' on Godot's values");
		return l_.default_value(p_result, p_position);
	}
	uint32_t out = l_.engine_reg(p_result, p_type);
	l_.emit(Op::HostOperate, p_position, out, *op, l_.arguments(std::move(p_operands)));
	return out;
}

} // namespace ir
} // namespace kas3
