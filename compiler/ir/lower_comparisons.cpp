// Comparisons and equality, for Operators: `<`, `<=`, `>`, `>=`, `==`, `!=`, `===` and `!==`, with an Int64
// against other kinds exactly.
#include "lower_engine.h"
#include "lower_operators.h"

namespace kas3 {
namespace ir {

// Where the left of an Int64 and a Boolean or a String stands against the right: below 0, 0 or above.
uint32_t Operators::wide_order(Expr *p_left, Expr *p_right, uint32_t p_position) {
	Kind left_kind = Kind::Void;
	l_.kind_of(p_left->type, &left_kind);
	const bool wide_first = left_kind == Kind::I64;
	Expr *other = wide_first ? p_right : p_left;
	Kind other_kind = Kind::Void;
	l_.kind_of(other->type, &other_kind);
	uint32_t x = l_.settled(l_.expression(p_left, wide_first ? Kind::I64 : (other_kind == Kind::Str ? Kind::Str : Kind::F64)), p_right, p_position);
	uint32_t y = l_.expression(p_right, wide_first ? (other_kind == Kind::Str ? Kind::Str : Kind::F64) : Kind::I64);
	uint32_t order = l_.reg(Kind::F64);
	l_.emit(other_kind == Kind::Str ? Op::CmpI64Str : Op::CmpI64F64, p_position, order, wide_first ? x : y, wide_first ? y : x);
	if (wide_first) {
		return order;
	}
	uint32_t turned = l_.reg(Kind::F64);
	l_.emit(Op::NegF64, p_position, turned, order); // The Int64 was on the right.
	return turned;
}

uint32_t Operators::number_zero(uint32_t p_position) {
	uint32_t zero = l_.reg(Kind::F64);
	l_.emit(Op::ConstF64, p_position, zero, l_.number(0));
	return zero;
}

uint32_t Operators::comparison(Binary *p_binary) {
	if (l_.untyped(p_binary->left) || l_.untyped(p_binary->right)) {
		return any_comparison(p_binary);
	}
	uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
	if (is_engine(l_.natural(left)) || is_engine(l_.natural(right))) {
		uint32_t a = l_.settled(Engine(l_).operand(left), right, position);
		uint32_t b = Engine(l_).operand(right);
		return Engine(l_).engine_operate(p_binary->op, { a, b }, Kind::Bool, nullptr, position);
	}
	bool swap_sides = p_binary->op == TokenKind::Greater || p_binary->op == TokenKind::GreaterEqual;
	bool with_equal = p_binary->op == TokenKind::LessEqual || p_binary->op == TokenKind::GreaterEqual;
	Kind left_kind = l_.operand_kind(left, right), right_kind = l_.operand_kind(right, left);
	if ((left_kind == Kind::I64 || right_kind == Kind::I64) && Lowerer::is_integral(left_kind) && Lowerer::is_integral(right_kind)) {
		uint32_t a = l_.settled(l_.convert(l_.wide_operand(left, right_kind), Kind::I64, position), right, position);
		uint32_t b = l_.convert(l_.wide_operand(right, left_kind), Kind::I64, position);
		uint32_t out = l_.reg(Kind::Bool);
		l_.emit(with_equal ? Op::LeI64 : Op::LtI64, position, out, swap_sides ? b : a, swap_sides ? a : b);
		return out;
	}
	if ((left_kind == Kind::I64 && (right_kind == Kind::Bool || right_kind == Kind::Str)) ||
			(right_kind == Kind::I64 && (left_kind == Kind::Bool || left_kind == Kind::Str))) {
		// `order` places the left against the right, exactly.
		uint32_t order = wide_order(left, right, position);
		uint32_t zero = number_zero(position);
		uint32_t out = l_.reg(Kind::Bool);
		l_.emit(with_equal ? Op::LeF64 : Op::LtF64, position, out, swap_sides ? zero : order, swap_sides ? order : zero);
		return out;
	}
	if ((left_kind == Kind::I64 && right_kind == Kind::F64) || (left_kind == Kind::F64 && right_kind == Kind::I64)) {
		// Exactly: neither side is converted to the other's type.
		uint32_t a = l_.settled(l_.expression(left, Kind::Void), right, position);
		uint32_t b = l_.expression(right, Kind::Void);
		bool wide_first = left_kind == Kind::I64;
		uint32_t order = l_.reg(Kind::F64);
		l_.emit(Op::CmpI64F64, position, order, wide_first ? a : b, wide_first ? b : a);
		uint32_t zero = l_.reg(Kind::F64);
		l_.emit(Op::ConstF64, position, zero, l_.number(0));
		// `order` places the Int64 against the Number. Not-a-number makes every comparison false.
		bool wide_is_less = wide_first != swap_sides; // The question is "Int64 < Number" (or <=).
		uint32_t out = l_.reg(Kind::Bool);
		l_.emit(with_equal ? Op::LeF64 : Op::LtF64, position, out, wide_is_less ? order : zero, wide_is_less ? zero : order);
		return out;
	}
	bool strings = l_.is_string_type(left->type) && l_.is_string_type(right->type);
	Kind kind = strings ? Kind::Str : Kind::F64;
	uint32_t a = l_.settled(l_.expression(left, kind), right, position);
	uint32_t b = l_.expression(right, kind);
	bool swap = p_binary->op == TokenKind::Greater || p_binary->op == TokenKind::GreaterEqual;
	bool or_equal = p_binary->op == TokenKind::LessEqual || p_binary->op == TokenKind::GreaterEqual;
	uint32_t out = l_.reg(Kind::Bool);
	l_.emit(strings ? (or_equal ? Op::LeStr : Op::LtStr) : (or_equal ? Op::LeF64 : Op::LtF64), position, out, swap ? b : a,
			swap ? a : b);
	return out;
}

// A Godot object or value against null: is there one? An object Godot has freed is not there.
uint32_t Operators::engine_against_null(Binary *p_binary, Expr *p_value, Kind p_kind) {
	const uint32_t position = p_binary->offset;
	uint32_t object = l_.expression(p_value, p_kind);
	uint32_t present = l_.reg(Kind::Bool);
	l_.emit(Op::HostPresent, position, present, object);
	if (p_binary->op == TokenKind::NotEqual || p_binary->op == TokenKind::StrictNotEqual) {
		return present;
	}
	uint32_t absent = l_.reg(Kind::Bool);
	l_.emit(Op::Not, position, absent, present);
	return absent;
}

// An Int64 against a Boolean or a String: exactly, as numbers.
uint32_t Operators::wide_against_other(Binary *p_binary) {
	const uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
	uint32_t out = l_.reg(Kind::Bool);
	l_.emit(Op::EqF64, position, out, wide_order(left, right, position), number_zero(position));
	if (p_binary->op == TokenKind::NotEqual) {
		uint32_t negated = l_.reg(Kind::Bool);
		l_.emit(Op::Not, position, negated, out);
		return negated;
	}
	return out;
}

// An Int64 against a number: as two Int64s when both are whole, else the Int64 and the Number exactly.
uint32_t Operators::wide_equality(Binary *p_binary) {
	const uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
	Kind x_kind = l_.operand_kind(left, right), y_kind = l_.operand_kind(right, left);
	uint32_t out = l_.reg(Kind::Bool);
	if (Lowerer::is_integral(x_kind) && Lowerer::is_integral(y_kind)) {
		uint32_t x = l_.settled(l_.convert(l_.wide_operand(left, y_kind), Kind::I64, position), right, position);
		uint32_t y = l_.convert(l_.wide_operand(right, x_kind), Kind::I64, position);
		l_.emit(Op::EqI64, position, out, x, y);
	} else {
		// An Int64 and a Number, exactly.
		uint32_t x = l_.settled(l_.expression(left, Kind::Void), right, position);
		uint32_t y = l_.expression(right, Kind::Void);
		uint32_t order = l_.reg(Kind::F64);
		l_.emit(Op::CmpI64F64, position, order, x_kind == Kind::I64 ? x : y, x_kind == Kind::I64 ? y : x);
		uint32_t zero = l_.reg(Kind::F64);
		l_.emit(Op::ConstF64, position, zero, l_.number(0));
		l_.emit(Op::EqF64, position, out, order, zero);
	}
	if (p_binary->op == TokenKind::NotEqual || p_binary->op == TokenKind::StrictNotEqual) {
		uint32_t negated = l_.reg(Kind::Bool);
		l_.emit(Op::Not, position, negated, out);
		return negated;
	}
	return out;
}

uint32_t Operators::equality(Binary *p_binary) {
	if (l_.untyped(p_binary->left) || l_.untyped(p_binary->right)) {
		return any_equality(p_binary);
	}
	uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
	Kind a = Lowerer::is_null(left) ? Kind::Void : l_.natural(left);
	Kind b = Lowerer::is_null(right) ? Kind::Void : l_.natural(right);
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
			return engine_against_null(p_binary, a == Kind::Void ? right : left, kind);
		}
		if (!is_reference(kind)) {
			l_.refuse(position, "comparing a number or a Boolean with null");
		}
		op = kind == Kind::Str ? Op::EqStr : Op::EqRef;
	} else if (is_engine(a) || is_engine(b)) {
		// Godot's own equality: the same object, or equal values.
		uint32_t x = l_.settled(Engine(l_).operand(left), right, position);
		uint32_t y = Engine(l_).operand(right);
		return Engine(l_).engine_operate(p_binary->op, { x, y }, Kind::Bool, nullptr, position);
	} else if (a == Kind::Str && b == Kind::Str) {
		kind = Kind::Str;
		op = Op::EqStr;
	} else if (a == Kind::Obj && b == Kind::Obj) {
		kind = Kind::Obj;
		op = Op::EqRef;
	} else if (a == Kind::Bool && b == Kind::Bool) {
		kind = Kind::Bool;
		op = Op::EqBool;
	} else if (!strict && ((a == Kind::I64 && (b == Kind::Bool || b == Kind::Str)) || (b == Kind::I64 && (a == Kind::Bool || a == Kind::Str)))) {
		return wide_against_other(p_binary);
	} else if ((l_.operand_kind(left, right) == Kind::I64 || l_.operand_kind(right, left) == Kind::I64) &&
			Lowerer::is_numeric(l_.operand_kind(left, right)) && Lowerer::is_numeric(l_.operand_kind(right, left))) {
		return wide_equality(p_binary);
	} else if ((Lowerer::is_numeric(a) && Lowerer::is_numeric(b)) || (!strict && (Lowerer::is_numeric(a) || a == Kind::Bool) && (Lowerer::is_numeric(b) || b == Kind::Bool))) {
		kind = Kind::F64;
	} else {
		l_.refuse(position, "comparing values of these two types");
	}
	uint32_t x = l_.settled(l_.expression(left, kind), right, position);
	uint32_t y = l_.expression(right, kind);
	uint32_t out = l_.reg(Kind::Bool);
	l_.emit(op, position, out, x, y);
	if (p_binary->op == TokenKind::NotEqual || p_binary->op == TokenKind::StrictNotEqual) {
		uint32_t negated = l_.reg(Kind::Bool);
		l_.emit(Op::Not, position, negated, out);
		return negated;
	}
	return out;
}

} // namespace ir
} // namespace kas3
