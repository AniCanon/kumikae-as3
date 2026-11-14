#include "typer.h"

namespace kas3 {

const Type * Typer::compute(Expr *p_expression) {
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
			return symbol_type(static_cast<Name *>(p_expression)->symbol, nullptr);
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
			return compilation_.vector_type(compilation_.type_of(literal->element_type));
		}
		case NodeKind::FunctionExpr:
			closures_++;
			function(static_cast<FunctionExpr *>(p_expression)->function);
			closures_--;
			return function_;
		case NodeKind::Member: {
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
		case NodeKind::Index: {
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
		case NodeKind::TypeApply: {
			TypeApply *apply = static_cast<TypeApply *>(p_expression);
			const Type *base = expression(apply->base);
			if (base->kind == Type::ClassObject && base->element->kind == Type::Vector) {
				return compilation_.class_object_type(compilation_.vector_type(compilation_.type_of(apply->argument)));
			}
			return unknown_;
		}
		case NodeKind::Call:
			return call(static_cast<Call *>(p_expression));
		case NodeKind::New: {
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
				return callee->element;
			}
			return callee->kind == Type::Any || dynamic_value(callee) ? any_ : unknown_;
		}
		case NodeKind::Unary:
			return unary(static_cast<Unary *>(p_expression));
		case NodeKind::Postfix: {
			const Type *operand = expression(static_cast<Postfix *>(p_expression)->operand);
			if (never_operator(static_cast<Postfix *>(p_expression)->op, operand, p_expression->offset)) {
				return unknown_;
			}
			written(static_cast<Postfix *>(p_expression)->operand);
			return numeric(operand) ? operand : (primitive(operand) ? number_ : unknown_);
		}
		case NodeKind::Binary:
			return binary(static_cast<Binary *>(p_expression));
		case NodeKind::Assign: {
			Assign *assign = static_cast<Assign *>(p_expression);
			const Type *target = expression(assign->target);
			const Type *value = expression(assign->value);
			written(assign->target);
			if (assign->op == TokenKind::Assign) {
				store(assign->value, target);
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
			if (bitwise && never_operator(assign->op, target, assign->offset)) {
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
		case NodeKind::Conditional: {
			Conditional *choice = static_cast<Conditional *>(p_expression);
			expression(choice->condition);
			const Type *then_type = expression(choice->then_value);
			const Type *else_type = expression(choice->else_value);
			return join(then_type, else_type);
		}
		default:
			return unknown_;
	}
}

const Type * Typer::expression(Expr *p_expression) {
	if (!p_expression) {
		return unknown_;
	}
	p_expression->type = compute(p_expression);
	return p_expression->type;
}

} // namespace kas3
