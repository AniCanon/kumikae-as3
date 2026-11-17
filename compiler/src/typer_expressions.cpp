#include "typer.h"

namespace kas3 {

const Type *Typer::member_expression_type(Expr *p_expression) {
	Member *member = static_cast<Member *>(p_expression);
	if (member->symbol) {
		// A definition named by its package. The path below it is not a value.
		for (Expr *part = member->object; part; part = part->kind == NodeKind::Member ? static_cast<Member *>(part)->object : nullptr) {
			part->type = unknown_;
		}
		return symbol_type(member->symbol, nullptr);
	}
	return member_type(expression(member->object), member);
}

const Type *Typer::index_type(Expr *p_expression) {
	Index *index = static_cast<Index *>(p_expression);
	const Type *object = expression(index->object);
	const Type *key = expression(index->index);
	if (object->kind == Type::Vector) {
		// Only a number reaches the elements; `v["length"]` is a property.
		return numeric(key) ? object->element : (key->kind == Type::Unknown ? unknown_ : any_);
	}
	if (object->kind == Type::Unknown || object->kind == Type::Void || object->kind == Type::Null) {
		return unknown_;
	}
	if (const Described *rules = described(object)) {
		// A number reaches the elements. Any other key may name a property, as in `v["x"]`.
		if (rules->element && (rules->keyed || numeric(key))) {
			return rules->element;
		}
	}
	return any_;
}

const Type *Typer::new_type(Expr *p_expression) {
	New *create = static_cast<New *>(p_expression);
	list(create->arguments);
	const Type *callee = expression(create->callee);
	if (callee->kind == Type::ClassObject) {
		if (abstract(callee->element)) {
			error(name_offset(create->callee),
					quoted(callee->element->symbol->name) + " can't be created with 'new'");
			return callee->element;
		}
		construct(callee->element, create->arguments, name_offset(create->callee));
		constructor_form(callee->element, create->arguments, name_offset(create->callee));
		return callee->element;
	}
	return callee->kind == Type::Any || dynamic_value(callee) ? any_ : unknown_;
}

const Type *Typer::assign_type(Expr *p_expression) {
	Assign *assign = static_cast<Assign *>(p_expression);
	Expr *outer = assigned_;
	assigned_ = assign->op == TokenKind::Assign ? assign->target : nullptr;
	const Type *target = expression(assign->target);
	assigned_ = outer;
	const Type *value = expression(assign->value);
	written(assign->target);
	if (assign->op == TokenKind::Assign) {
		store(assign->value, target);
		if (assign->target->kind == NodeKind::Index) {
			const Type *holder = static_cast<Index *>(assign->target)->object ? static_cast<Index *>(assign->target)->object->type : nullptr;
			no_null_place(assign->value, holder && holder->kind == Type::Vector ? holder->element : nullptr);
		}
	}
	if (target->kind == Type::Unknown) {
		return unknown_;
	}
	bool bitwise = false;
	switch (assign->op) {
		case TokenKind::AmpAssign:
		case TokenKind::PipeAssign:
		case TokenKind::CaretAssign:
		case TokenKind::ShiftLeftAssign:
		case TokenKind::ShiftRightAssign:
		case TokenKind::ShiftRightUnsignedAssign:
			bitwise = true;
			break;
		default:
			break;
	}
	// No Godot type has a bit operation, on either side.
	if (bitwise && (never_operator(assign->op, target, assign->offset) || never_operator(assign->op, value, assign->offset))) {
		return target;
	}
	TokenKind plain = compound_operator(assign->op);
	if (plain != TokenKind::Error && !(plain == TokenKind::Plus && (target == string_ || value == string_))) {
		if (const Type *result = described_binary(plain, assign->target, assign->value, assign->offset)) {
			// Value types don't convert to each other, so a result of another type can't go back.
			if (result->kind != Type::Unknown && target->kind != Type::Any &&
					fits(result, target) != Fit::Yes) {
				error(assign->offset, "can't use a value of type " + quoted(compilation_.type_name(result)) +
											  " where " + quoted(compilation_.type_name(target)) +
											  " is expected");
			}
			return target;
		}
	}
	if (assign->op == TokenKind::Assign || (numeric(target) && !bitwise)) {
		return target; // The value, converted to what it is stored in.
	}
	if (assign->op == TokenKind::AmpAmpAssign || assign->op == TokenKind::PipePipeAssign) {
		return target;
	}
	return arithmetic(assign->op, target, value);
}

const Type *Typer::literal_type(Expr *p_expression) {
	switch (p_expression->kind) {
		case NodeKind::NumberLit:
			return number_literal(static_cast<NumberLit *>(p_expression));
		case NodeKind::StringLit:
			return string_;
		case NodeKind::RegexLit:
			return regexp_;
		case NodeKind::BoolLit:
			return boolean_;
		case NodeKind::NullLit:
			return compilation_.null_type();
		case NodeKind::ArrayLit:
			list(static_cast<ArrayLit *>(p_expression)->elements);
			return array_;
		case NodeKind::ObjectLit:
			for (ObjectField &field : static_cast<ObjectLit *>(p_expression)->fields) {
				expression(field.value);
			}
			return object_;
		case NodeKind::VectorLit: {
			VectorLit *literal = static_cast<VectorLit *>(p_expression);
			list(literal->elements);
			const Type *element = compilation_.type_of(literal->element_type);
			for (const Expr *each : literal->elements) {
				no_null_place(each, element);
			}
			return compilation_.vector_type(element);
		}
		default:
			return unknown_;
	}
}

// `Vector.<T>` written as a value: the class of vectors of T.
const Type *Typer::applied_type(TypeApply *p_apply) {
	const Type *base = expression(p_apply->base);
	if (base->kind == Type::ClassObject && base->element->kind == Type::Vector) {
		return compilation_.class_object_type(compilation_.vector_type(compilation_.type_of(p_apply->argument)));
	}
	return unknown_;
}

const Type *Typer::postfix_type(Postfix *p_postfix) {
	const Type *operand = expression(p_postfix->operand);
	if (never_operator(p_postfix->op, operand, p_postfix->offset)) {
		return unknown_;
	}
	written(p_postfix->operand);
	return numeric(operand) ? operand : (primitive(operand) ? number_ : unknown_);
}

const Type *Typer::conditional_type(Conditional *p_choice) {
	expression(p_choice->condition);
	const Type *then_type = expression(p_choice->then_value);
	const Type *else_type = expression(p_choice->else_value);
	return join(then_type, else_type);
}

const Type *Typer::compute(Expr *p_expression) {
	switch (p_expression->kind) {
		case NodeKind::NumberLit:
		case NodeKind::StringLit:
		case NodeKind::RegexLit:
		case NodeKind::BoolLit:
		case NodeKind::NullLit:
		case NodeKind::ArrayLit:
		case NodeKind::ObjectLit:
		case NodeKind::VectorLit:
			return literal_type(p_expression);
		case NodeKind::ThisExpr:
			if (static_) {
				return unknown_; // Reported by the names pass.
			}
			// In a nested function, `this` is whatever the caller supplies.
			return class_ && closures_ == 0 ? compilation_.instance_type(class_) : any_;
		case NodeKind::SuperExpr:
			return class_ && !static_ && class_->klass->base ? compilation_.instance_type(class_->klass->base)
															 : unknown_;
		case NodeKind::Name:
			read(p_expression);
			return symbol_type(static_cast<Name *>(p_expression)->symbol, nullptr);
		case NodeKind::FunctionExpr:
			closures_++;
			function(static_cast<FunctionExpr *>(p_expression)->function);
			closures_--;
			return function_;
		case NodeKind::Member: {
			const Type *type = member_expression_type(p_expression);
			read(p_expression);
			return type;
		}
		case NodeKind::Index:
			return index_type(p_expression);
		case NodeKind::TypeApply:
			return applied_type(static_cast<TypeApply *>(p_expression));
		case NodeKind::Call:
			return call(static_cast<Call *>(p_expression));
		case NodeKind::New:
			return new_type(p_expression);
		case NodeKind::Unary:
			return unary(static_cast<Unary *>(p_expression));
		case NodeKind::Postfix:
			return postfix_type(static_cast<Postfix *>(p_expression));
		case NodeKind::Binary:
			return binary(static_cast<Binary *>(p_expression));
		case NodeKind::Assign:
			return assign_type(p_expression);
		case NodeKind::Conditional:
			return conditional_type(static_cast<Conditional *>(p_expression));
		default:
			return unknown_;
	}
}

const Type *Typer::expression(Expr *p_expression) {
	if (!p_expression) {
		return unknown_;
	}
	p_expression->type = compute(p_expression);
	return p_expression->type;
}

} // namespace kas3
