#include "lowerer.h"

namespace kas3 {
namespace ir {

bool Lowerer::whole(const Expr *p_expression) {
	if (!p_expression || !p_expression->type) {
		return false;
	}
	if (p_expression->kind == NodeKind::Binary) {
		const Binary *binary = static_cast<const Binary *>(p_expression);
		switch (binary->op) {
			case TokenKind::Plus:
			case TokenKind::Minus:
			case TokenKind::Star:
			case TokenKind::Percent:
				return whole(binary->left) && whole(binary->right);
			default:
				break;
		}
	}
	if (p_expression->kind == NodeKind::Unary) {
		const Unary *unary = static_cast<const Unary *>(p_expression);
		if (unary->op == TokenKind::Minus || unary->op == TokenKind::Plus) {
			return whole(unary->operand);
		}
	}
	if (p_expression->kind == NodeKind::Conditional) {
		const Conditional *choice = static_cast<const Conditional *>(p_expression);
		return whole(choice->then_value) && whole(choice->else_value);
	}
	if (p_expression->kind == NodeKind::Call) {
		const Call *call = static_cast<const Call *>(p_expression);
		const Symbol *symbol = call->callee && call->callee->kind == NodeKind::Member
				? (static_cast<const Member *>(call->callee)->symbol ? static_cast<const Member *>(call->callee)->symbol : static_cast<const Member *>(call->callee)->member)
				: call->callee && call->callee->kind == NodeKind::Name ? static_cast<const Name *>(call->callee)->symbol
																		: nullptr;
		if (symbol && symbol->unit && symbol->unit->is_builtin) {
			bool math = symbol->owner && symbol->owner->name == "Math" && symbol->kind == SymbolKind::Method;
			if ((math && (symbol->name == "floor" || symbol->name == "round" || symbol->name == "ceil")) ||
					(!symbol->owner && symbol->kind == SymbolKind::Function && symbol->name == "parseInt")) {
				return true;
			}
			if (math && (symbol->name == "abs" || symbol->name == "max" || symbol->name == "min") && call->arguments.count > 0) {
				for (const Expr *argument : call->arguments) {
					if (!whole(argument)) {
						return false;
					}
				}
				return true;
			}
		}
	}
	Kind kind = Kind::Void;
	return kind_of(p_expression->type, &kind) && is_integral(kind);
}

uint32_t Lowerer::operate(TokenKind p_op, uint32_t p_left, uint32_t p_right, bool p_string, uint32_t p_position) {
	if (p_string) {
		uint32_t left = reg(Kind::Str), right = reg(Kind::Str), out = reg(Kind::Str);
		emit(Op::StrToStr, p_position, left, convert(p_left, Kind::Str, p_position));
		emit(Op::StrToStr, p_position, right, convert(p_right, Kind::Str, p_position));
		emit(Op::Concat, p_position, out, left, right);
		return out;
	}
	if (kind_at(p_left) == Kind::I64 || kind_at(p_right) == Kind::I64) {
		// With an Int64, whole-number arithmetic stays in 64 bits. Division, and anything with a
		// Number, falls through to Numbers: the Int64 converts, and says so if it does not fit.
		Op wide_op = Op::Stmt;
		bool whole_only = true;
		switch (p_op) {
			case TokenKind::Plus:
			case TokenKind::PlusAssign:
				wide_op = Op::AddI64;
				break;
			case TokenKind::Minus:
			case TokenKind::MinusAssign:
				wide_op = Op::SubI64;
				break;
			case TokenKind::Star:
			case TokenKind::StarAssign:
				wide_op = Op::MulI64;
				break;
			case TokenKind::Percent:
			case TokenKind::PercentAssign:
				wide_op = Op::ModI64;
				break;
			case TokenKind::Amp:
			case TokenKind::AmpAssign:
				wide_op = Op::BitAndI64;
				whole_only = false;
				break;
			case TokenKind::Pipe:
			case TokenKind::PipeAssign:
				wide_op = Op::BitOrI64;
				whole_only = false;
				break;
			case TokenKind::Caret:
			case TokenKind::CaretAssign:
				wide_op = Op::BitXorI64;
				whole_only = false;
				break;
			case TokenKind::ShiftLeft:
			case TokenKind::ShiftLeftAssign:
				wide_op = Op::ShlI64;
				whole_only = false;
				break;
			case TokenKind::ShiftRight:
			case TokenKind::ShiftRightAssign:
				wide_op = Op::ShrI64;
				whole_only = false;
				break;
			case TokenKind::ShiftRightUnsigned:
			case TokenKind::ShiftRightUnsignedAssign:
				wide_op = Op::UShrI64;
				whole_only = false;
				break;
			default:
				break;
		}
		if (wide_op != Op::Stmt && (!whole_only || (is_integral(kind_at(p_left)) && is_integral(kind_at(p_right))))) {
			uint32_t left = convert(p_left, Kind::I64, p_position);
			uint32_t right = convert(p_right, Kind::I64, p_position);
			uint32_t out = reg(Kind::I64);
			emit(wide_op, p_position, out, left, right);
			return out;
		}
	}
	Op op = Op::AddF64;
	Kind operands = Kind::F64;
	Kind result = Kind::F64;
	switch (p_op) {
		case TokenKind::Plus:
		case TokenKind::PlusAssign:
			op = Op::AddF64;
			break;
		case TokenKind::Minus:
		case TokenKind::MinusAssign:
			op = Op::SubF64;
			break;
		case TokenKind::Star:
		case TokenKind::StarAssign:
			op = Op::MulF64;
			break;
		case TokenKind::Slash:
		case TokenKind::SlashAssign:
			op = Op::DivF64;
			break;
		case TokenKind::Percent:
		case TokenKind::PercentAssign:
			op = Op::ModF64;
			break;
		case TokenKind::Amp:
		case TokenKind::AmpAssign:
			op = Op::BitAnd;
			operands = result = Kind::I32;
			break;
		case TokenKind::Pipe:
		case TokenKind::PipeAssign:
			op = Op::BitOr;
			operands = result = Kind::I32;
			break;
		case TokenKind::Caret:
		case TokenKind::CaretAssign:
			op = Op::BitXor;
			operands = result = Kind::I32;
			break;
		case TokenKind::ShiftLeft:
		case TokenKind::ShiftLeftAssign:
			op = Op::Shl;
			operands = result = Kind::I32;
			break;
		case TokenKind::ShiftRight:
		case TokenKind::ShiftRightAssign:
			op = Op::Shr;
			operands = result = Kind::I32;
			break;
		case TokenKind::ShiftRightUnsigned:
		case TokenKind::ShiftRightUnsignedAssign:
			op = Op::UShr;
			operands = Kind::I32;
			result = Kind::U32;
			break;
		default:
			refuse(p_position, "this operator");
			break;
	}
	uint32_t out = reg(result);
	uint32_t left = convert(p_left, op == Op::UShr ? Kind::U32 : operands, p_position);
	emit(op, p_position, out, left, convert(p_right, operands, p_position));
	return out;
}

uint32_t Lowerer::binary(Binary *p_binary, Kind p_want) {
	uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
	switch (p_binary->op) {
		case TokenKind::AmpAmp:
		case TokenKind::PipePipe: {
			// As a condition it is a Boolean; otherwise it is one of its operands, which must then be
			// of one kind.
			Kind kind = Kind::Bool;
			if (p_want != Kind::Bool) {
				Kind a = is_null(left) ? Kind::Void : natural(left);
				Kind b = is_null(right) ? Kind::Void : natural(right);
				if (a != b && is_integral(a) && is_integral(b) && (a == Kind::I64 || b == Kind::I64)) {
					a = b = Kind::I64; // An Int64 holds both, as the checker took it.
				}
				kind = a == Kind::Void ? b : a;
				if (kind == Kind::Void || (a != Kind::Void && b != Kind::Void && a != b)) {
					refuse(position, "'&&' or '||' between values of different types");
					kind = Kind::Bool;
				}
			}
			uint32_t out = reg(kind);
			emit(Op::Move, position, out, expression(left, kind));
			uint32_t test = convert(out, Kind::Bool, position);
			uint32_t skip = emit(p_binary->op == TokenKind::AmpAmp ? Op::JumpIfNot : Op::JumpIf, position, test, 0);
			emit(Op::Move, position, out, expression(right, kind));
			patch(skip, here());
			return out;
		}
		case TokenKind::Comma:
			expression(left, Kind::Void);
			return expression(right, p_want);
		case TokenKind::KwIs:
		case TokenKind::KwAs: {
			Symbol *symbol = named_class(right);
			uint32_t index = 0;
			if (!symbol || !class_index(symbol, &index)) {
				refuse(right->offset, std::string(p_binary->op == TokenKind::KwIs ? "'is'" : "'as'") + " with this type");
				return default_value(p_binary->op == TokenKind::KwIs ? Kind::Bool : Kind::Obj, position);
			}
			uint32_t object = expression(left, Kind::Void);
			if (kind_at(object) != Kind::Obj) {
				refuse(left->offset, std::string(p_binary->op == TokenKind::KwIs ? "'is'" : "'as'") + " on a value that is not an object");
				return default_value(Kind::Bool, position);
			}
			uint32_t out = reg(p_binary->op == TokenKind::KwIs ? Kind::Bool : Kind::Obj);
			emit(p_binary->op == TokenKind::KwIs ? Op::IsClass : Op::AsClass, position, out, object, index);
			return out;
		}
		case TokenKind::Less:
		case TokenKind::Greater:
		case TokenKind::LessEqual:
		case TokenKind::GreaterEqual: {
			if (is_engine(natural(left)) || is_engine(natural(right))) {
				uint32_t a = settled(operand(left), right, position);
				uint32_t b = operand(right);
				return engine_operate(p_binary->op, { a, b }, Kind::Bool, nullptr, position);
			}
			bool swap_sides = p_binary->op == TokenKind::Greater || p_binary->op == TokenKind::GreaterEqual;
			bool with_equal = p_binary->op == TokenKind::LessEqual || p_binary->op == TokenKind::GreaterEqual;
			Kind left_kind = operand_kind(left, right), right_kind = operand_kind(right, left);
			if ((left_kind == Kind::I64 || right_kind == Kind::I64) && is_integral(left_kind) && is_integral(right_kind)) {
				uint32_t a = settled(convert(wide_operand(left, right_kind), Kind::I64, position), right, position);
				uint32_t b = convert(wide_operand(right, left_kind), Kind::I64, position);
				uint32_t out = reg(Kind::Bool);
				emit(with_equal ? Op::LeI64 : Op::LtI64, position, out, swap_sides ? b : a, swap_sides ? a : b);
				return out;
			}
			if ((left_kind == Kind::I64 && right_kind == Kind::F64) || (left_kind == Kind::F64 && right_kind == Kind::I64)) {
				// Exactly: neither side is converted to the other's type.
				uint32_t a = settled(expression(left, Kind::Void), right, position);
				uint32_t b = expression(right, Kind::Void);
				bool wide_first = left_kind == Kind::I64;
				uint32_t order = reg(Kind::F64);
				emit(Op::CmpI64F64, position, order, wide_first ? a : b, wide_first ? b : a);
				uint32_t zero = reg(Kind::F64);
				emit(Op::ConstF64, position, zero, number(0));
				// `order` places the Int64 against the Number. Not-a-number makes every comparison false.
				bool wide_is_less = wide_first != swap_sides; // The question is "Int64 < Number" (or <=).
				uint32_t out = reg(Kind::Bool);
				emit(with_equal ? Op::LeF64 : Op::LtF64, position, out, wide_is_less ? order : zero, wide_is_less ? zero : order);
				return out;
			}
			bool strings = is_string_type(left->type) && is_string_type(right->type);
			Kind kind = strings ? Kind::Str : Kind::F64;
			uint32_t a = settled(expression(left, kind), right, position);
			uint32_t b = expression(right, kind);
			bool swap = p_binary->op == TokenKind::Greater || p_binary->op == TokenKind::GreaterEqual;
			bool or_equal = p_binary->op == TokenKind::LessEqual || p_binary->op == TokenKind::GreaterEqual;
			uint32_t out = reg(Kind::Bool);
			emit(strings ? (or_equal ? Op::LeStr : Op::LtStr) : (or_equal ? Op::LeF64 : Op::LtF64), position, out, swap ? b : a,
					swap ? a : b);
			return out;
		}
		case TokenKind::Equal:
		case TokenKind::NotEqual:
		case TokenKind::StrictEqual:
		case TokenKind::StrictNotEqual: {
			Kind a = is_null(left) ? Kind::Void : natural(left);
			Kind b = is_null(right) ? Kind::Void : natural(right);
			bool strict = p_binary->op == TokenKind::StrictEqual || p_binary->op == TokenKind::StrictNotEqual;
			Kind kind = Kind::F64;
			Op op = Op::EqF64;
			if (a == Kind::Void && b == Kind::Void) {
				kind = Kind::Obj;
				op = Op::EqRef;
			} else if (a == Kind::Void || b == Kind::Void) {
				// Against null: a reference is compared; a number or a Boolean is never null.
				kind = a == Kind::Void ? b : a;
				if (is_engine(kind)) {
					// A Godot object or value against null: is there one?
					uint32_t object = expression(a == Kind::Void ? right : left, kind);
					uint32_t present = reg(Kind::Bool);
					emit(Op::ObjToBool, position, present, object);
					if (p_binary->op == TokenKind::NotEqual || p_binary->op == TokenKind::StrictNotEqual) {
						return present;
					}
					uint32_t absent = reg(Kind::Bool);
					emit(Op::Not, position, absent, present);
					return absent;
				}
				if (!is_reference(kind)) {
					refuse(position, "comparing a number or a Boolean with null");
				}
				op = kind == Kind::Str ? Op::EqStr : Op::EqRef;
			} else if (is_engine(a) || is_engine(b)) {
				// Godot's own equality: the same object, or equal values.
				uint32_t x = settled(operand(left), right, position);
				uint32_t y = operand(right);
				return engine_operate(p_binary->op, { x, y }, Kind::Bool, nullptr, position);
			} else if (a == Kind::Str && b == Kind::Str) {
				kind = Kind::Str;
				op = Op::EqStr;
			} else if (a == Kind::Obj && b == Kind::Obj) {
				kind = Kind::Obj;
				op = Op::EqRef;
			} else if (a == Kind::Bool && b == Kind::Bool) {
				kind = Kind::Bool;
				op = Op::EqBool;
			} else if ((operand_kind(left, right) == Kind::I64 || operand_kind(right, left) == Kind::I64) &&
					is_numeric(operand_kind(left, right)) && is_numeric(operand_kind(right, left))) {
				Kind x_kind = operand_kind(left, right), y_kind = operand_kind(right, left);
				uint32_t out = reg(Kind::Bool);
				if (is_integral(x_kind) && is_integral(y_kind)) {
					uint32_t x = settled(convert(wide_operand(left, y_kind), Kind::I64, position), right, position);
					uint32_t y = convert(wide_operand(right, x_kind), Kind::I64, position);
					emit(Op::EqI64, position, out, x, y);
				} else {
					// An Int64 and a Number, exactly.
					uint32_t x = settled(expression(left, Kind::Void), right, position);
					uint32_t y = expression(right, Kind::Void);
					uint32_t order = reg(Kind::F64);
					emit(Op::CmpI64F64, position, order, x_kind == Kind::I64 ? x : y, x_kind == Kind::I64 ? y : x);
					uint32_t zero = reg(Kind::F64);
					emit(Op::ConstF64, position, zero, number(0));
					emit(Op::EqF64, position, out, order, zero);
				}
				if (p_binary->op == TokenKind::NotEqual || p_binary->op == TokenKind::StrictNotEqual) {
					uint32_t negated = reg(Kind::Bool);
					emit(Op::Not, position, negated, out);
					return negated;
				}
				return out;
			} else if ((is_numeric(a) && is_numeric(b)) || (!strict && (is_numeric(a) || a == Kind::Bool) && (is_numeric(b) || b == Kind::Bool))) {
				kind = Kind::F64;
			} else {
				refuse(position, "comparing values of these two types");
			}
			uint32_t x = settled(expression(left, kind), right, position);
			uint32_t y = expression(right, kind);
			uint32_t out = reg(Kind::Bool);
			emit(op, position, out, x, y);
			if (p_binary->op == TokenKind::NotEqual || p_binary->op == TokenKind::StrictNotEqual) {
				uint32_t negated = reg(Kind::Bool);
				emit(Op::Not, position, negated, out);
				return negated;
			}
			return out;
		}
		case TokenKind::KwIn:
		case TokenKind::KwInstanceof:
			refuse(position, p_binary->op == TokenKind::KwIn ? "'in'" : "'instanceof'");
			return default_value(Kind::Bool, position);
		default: {
			Kind left_kind = Kind::Void, right_kind = Kind::Void;
			kind_of(left->type, &left_kind);
			kind_of(right->type, &right_kind);
			bool strings = p_binary->op == TokenKind::Plus && is_string_type(p_binary->type);
			if (!strings && (is_engine(left_kind) || is_engine(right_kind))) {
				Kind result = Kind::Void;
				if (!kind_of(p_binary->type, &result) || result == Kind::Void) {
					refuse(position, "this operator on a Godot value");
					return default_value(p_want, position);
				}
				uint32_t a = settled(operand(left, right->type), right, position);
				uint32_t b = operand(right, left->type);
				return engine_operate(p_binary->op, { a, b }, result, p_binary->type, position);
			}
			// Division is done in Numbers, so a literal beside an Int64 stays the Number it is written as.
			bool exact = !strings && p_binary->op != TokenKind::Slash;
			uint32_t a = settled(wide_operand(left, exact ? right_kind : Kind::Void), right, position);
			uint32_t b = wide_operand(right, exact ? left_kind : Kind::Void);
			return operate(p_binary->op, a, b, strings, position);
		}
	}
}

} // namespace ir
} // namespace kas3
