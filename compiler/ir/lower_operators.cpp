#include "lower_dynamic.h"

namespace kas3 {
namespace ir {

bool Lowerer::whole(const Expr *p_expression) {
	return is_whole_number(p_expression, [this](const Expr *p_operand) {
		Kind kind = Kind::Void;
		return kind_of(p_operand->type, &kind) && is_integral(kind);
	});
}

namespace {

// How one of AS3's arithmetic or bitwise operators is carried out. Without an Int64 it is done on
// Numbers, or on 32-bit integers for the bitwise ones. With one, whole-number arithmetic stays in 64
// bits; division has no such form, and neither has `+ - * %` when the other side is a Number.
struct Arithmetic {
	TokenKind op;
	TokenKind assign; // The same operator written with `=`.
	Op narrow;
	Kind operands;
	Kind result;
	Op wide; // Op::Stmt: none.
	bool wide_needs_whole; // Both sides must be whole numbers for the 64-bit form.
};

constexpr Arithmetic ARITHMETIC[] = {
	{ TokenKind::Plus, TokenKind::PlusAssign, Op::AddF64, Kind::F64, Kind::F64, Op::AddI64, true },
	{ TokenKind::Minus, TokenKind::MinusAssign, Op::SubF64, Kind::F64, Kind::F64, Op::SubI64, true },
	{ TokenKind::Star, TokenKind::StarAssign, Op::MulF64, Kind::F64, Kind::F64, Op::MulI64, true },
	{ TokenKind::Slash, TokenKind::SlashAssign, Op::DivF64, Kind::F64, Kind::F64, Op::Stmt, true },
	{ TokenKind::Percent, TokenKind::PercentAssign, Op::ModF64, Kind::F64, Kind::F64, Op::ModI64, true },
	{ TokenKind::Amp, TokenKind::AmpAssign, Op::BitAnd, Kind::I32, Kind::I32, Op::BitAndI64, false },
	{ TokenKind::Pipe, TokenKind::PipeAssign, Op::BitOr, Kind::I32, Kind::I32, Op::BitOrI64, false },
	{ TokenKind::Caret, TokenKind::CaretAssign, Op::BitXor, Kind::I32, Kind::I32, Op::BitXorI64, false },
	{ TokenKind::ShiftLeft, TokenKind::ShiftLeftAssign, Op::Shl, Kind::I32, Kind::I32, Op::ShlI64, false },
	{ TokenKind::ShiftRight, TokenKind::ShiftRightAssign, Op::Shr, Kind::I32, Kind::I32, Op::ShrI64, false },
	{ TokenKind::ShiftRightUnsigned, TokenKind::ShiftRightUnsignedAssign, Op::UShr, Kind::I32, Kind::U32, Op::UShrI64, false },
};

const Arithmetic *arithmetic_of(TokenKind p_op) {
	for (const Arithmetic &entry : ARITHMETIC) {
		if (entry.op == p_op || entry.assign == p_op) {
			return &entry;
		}
	}
	return nullptr;
}

} // namespace

// Two values as strings, one after the other.
uint32_t Lowerer::concatenate(uint32_t p_left, uint32_t p_right, uint32_t p_position) {
	uint32_t left = reg(Kind::Str), right = reg(Kind::Str), out = reg(Kind::Str);
	emit(Op::StrToStr, p_position, left, convert(p_left, Kind::Str, p_position));
	emit(Op::StrToStr, p_position, right, convert(p_right, Kind::Str, p_position));
	emit(Op::Concat, p_position, out, left, right);
	return out;
}

uint32_t Lowerer::operate(TokenKind p_op, uint32_t p_left, uint32_t p_right, bool p_string, uint32_t p_position) {
	if (p_string) {
		return concatenate(p_left, p_right, p_position);
	}
	const Arithmetic *how = arithmetic_of(p_op);
	if (!how) {
		refuse(p_position, "this operator");
		how = &ARITHMETIC[0];
	}
	// A shift is as wide as what is shifted; its count is only ever its low bits, so an Int64 count is cut.
	const bool shift = how->narrow == Op::Shl || how->narrow == Op::Shr || how->narrow == Op::UShr;
	if (shift && kind_at(p_left) != Kind::I64 && kind_at(p_right) == Kind::I64) {
		p_right = convert(p_right, Kind::I32, p_position, true);
	}
	if (kind_at(p_left) == Kind::I64 || kind_at(p_right) == Kind::I64) {
		// Anything that has no 64-bit form falls through to Numbers: the Int64 converts, and says so if
		// it does not fit.
		const bool whole = is_integral(kind_at(p_left)) && is_integral(kind_at(p_right));
		if (how->wide != Op::Stmt && (!how->wide_needs_whole || whole)) {
			uint32_t left = convert(p_left, Kind::I64, p_position);
			uint32_t right = convert(p_right, Kind::I64, p_position);
			uint32_t out = reg(Kind::I64);
			emit(how->wide, p_position, out, left, right);
			return out;
		}
	}
	uint32_t out = reg(how->result);
	uint32_t left = convert(p_left, how->narrow == Op::UShr ? Kind::U32 : how->operands, p_position);
	emit(how->narrow, p_position, out, left, convert(p_right, how->operands, p_position));
	return out;
}

uint32_t Lowerer::logical(Binary *p_binary, Kind p_want) {
	uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
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
		if (a == Kind::Any || b == Kind::Any) {
			kind = a = b = Kind::Any; // An untyped value holds either side.
		}
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

// The Godot object a script object is attached to, for `is` and `as` with one of Godot's classes. A
// script class that extends a Godot class holds it in its first field; an object of any other class of
// the program is no Godot object, and nor is no object at all: both are null.
uint32_t Lowerer::godot_object_of(uint32_t p_object, Expr *p_written, uint32_t p_position) {
	const Symbol *symbol = p_written->type && p_written->type->kind == Type::Instance ? p_written->type->symbol : nullptr;
	const auto found = symbol ? classes_.find(symbol) : classes_.end();
	const uint32_t owner = default_value(Kind::Ref, p_position);
	if (found == classes_.end() || module_.classes[found->second].native_base.empty()) {
		return owner;
	}
	const uint32_t skip = emit(Op::JumpIfNot, p_position, convert(p_object, Kind::Bool, p_position), 0);
	emit(Op::GetField, p_position, owner, p_object, 0);
	patch(skip, here());
	return owner;
}

uint32_t Lowerer::type_test(Binary *p_binary) {
	uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
	Symbol *symbol = named_class(right);
	uint32_t index = 0;
	// `v is Vector.<int>`: the type written is the class itself, and its instances are the vector.
	const Type *written = right->type && right->type->kind == Type::ClassObject ? right->type->element : nullptr;
	uint32_t godot_class = 0;
	const bool godot_target = symbol && is_godot(symbol) && is_godot_object(symbol);
	if (!godot_target && (untyped(left) || (!is_vector(written) && !is_null(left) && natural(left) != Kind::Obj))) {
		if (const std::optional<uint32_t> tested = any_type_test(p_binary)) {
			return *tested;
		}
	}
	if (godot_target && untyped(left) && engine_name(symbol, std::string_view(), &godot_class)) {
		// An untyped value and one of Godot's classes: whether it holds such an object decides.
		const bool test = p_binary->op == TokenKind::KwIs;
		const uint32_t value = any_value(left);
		uint32_t fits = reg(Kind::Bool);
		emit(Op::AnyIsGodot, position, fits, value, godot_class);
		if (test) {
			return fits;
		}
		uint32_t out = engine_reg(Kind::Ref, p_binary->type);
		emit(Op::ConstNull, position, out);
		const uint32_t skip = emit(Op::JumpIfNot, position, fits, 0);
		emit(Op::FromAny, position, out, value, godot_class);
		patch(skip, here());
		return out;
	}
	if (godot_target && engine_name(symbol, std::string_view(), &godot_class)) {
		// One of Godot's classes: the engine knows what an object of its own is.
		const bool test = p_binary->op == TokenKind::KwIs;
		uint32_t object = expression(left, Kind::Void);
		if (kind_at(object) == Kind::Obj) {
			object = godot_object_of(object, left, position);
		}
		if (kind_at(object) != Kind::Ref) {
			if (!test || kind_at(object) != Kind::Str) {
				refuse(left->offset, std::string(test ? "'is'" : "'as'") + " with a Godot class on a value that is not one of Godot's objects");
				return default_value(test ? Kind::Bool : Kind::Ref, position);
			}
			return default_value(Kind::Bool, position); // A string is never one of Godot's objects.
		}
		uint32_t out = test ? reg(Kind::Bool) : engine_reg(Kind::Ref, p_binary->type);
		emit(test ? Op::HostIsClass : Op::HostAsClass, position, out, object, godot_class);
		return out;
	}
	if (const std::optional<uint32_t> of_vector = is_vector(written) ? vector_class(written) : std::nullopt) {
		index = *of_vector;
	} else if (!symbol || !class_index(symbol, &index)) {
		refuse(right->offset, std::string(p_binary->op == TokenKind::KwIs ? "'is'" : "'as'") + " with this type");
		return default_value(p_binary->op == TokenKind::KwIs ? Kind::Bool : Kind::Obj, position);
	}
	uint32_t object = expression(left, Kind::Void);
	if (kind_at(object) != Kind::Obj) {
		refuse(left->offset, std::string(p_binary->op == TokenKind::KwIs ? "'is'" : "'as'") + " on a value that is not an object");
		return default_value(Kind::Bool, position);
	}
	if (symbol) {
		touch(symbol, position); // The class named is looked up after the value, and set up then, as in AVM2.
	}
	uint32_t out = reg(p_binary->op == TokenKind::KwIs ? Kind::Bool : Kind::Obj);
	emit(p_binary->op == TokenKind::KwIs ? Op::IsClass : Op::AsClass, position, out, object, index);
	return out;
}

// Where the left of an Int64 and a Boolean or a String stands against the right: below 0, 0 or above.
uint32_t Lowerer::wide_order(Expr *p_left, Expr *p_right, uint32_t p_position) {
	Kind left_kind = Kind::Void;
	kind_of(p_left->type, &left_kind);
	const bool wide_first = left_kind == Kind::I64;
	Expr *other = wide_first ? p_right : p_left;
	Kind other_kind = Kind::Void;
	kind_of(other->type, &other_kind);
	uint32_t x = settled(expression(p_left, wide_first ? Kind::I64 : (other_kind == Kind::Str ? Kind::Str : Kind::F64)), p_right, p_position);
	uint32_t y = expression(p_right, wide_first ? (other_kind == Kind::Str ? Kind::Str : Kind::F64) : Kind::I64);
	uint32_t order = reg(Kind::F64);
	emit(other_kind == Kind::Str ? Op::CmpI64Str : Op::CmpI64F64, p_position, order, wide_first ? x : y, wide_first ? y : x);
	if (wide_first) {
		return order;
	}
	uint32_t turned = reg(Kind::F64);
	emit(Op::NegF64, p_position, turned, order); // The Int64 was on the right.
	return turned;
}

uint32_t Lowerer::number_zero(uint32_t p_position) {
	uint32_t zero = reg(Kind::F64);
	emit(Op::ConstF64, p_position, zero, number(0));
	return zero;
}

uint32_t Lowerer::comparison(Binary *p_binary) {
	if (untyped(p_binary->left) || untyped(p_binary->right)) {
		return any_comparison(p_binary);
	}
	uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
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
	if ((left_kind == Kind::I64 && (right_kind == Kind::Bool || right_kind == Kind::Str)) ||
			(right_kind == Kind::I64 && (left_kind == Kind::Bool || left_kind == Kind::Str))) {
		// `order` places the left against the right, exactly.
		uint32_t order = wide_order(left, right, position);
		uint32_t zero = number_zero(position);
		uint32_t out = reg(Kind::Bool);
		emit(with_equal ? Op::LeF64 : Op::LtF64, position, out, swap_sides ? zero : order, swap_sides ? order : zero);
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

uint32_t Lowerer::equality(Binary *p_binary) {
	if (untyped(p_binary->left) || untyped(p_binary->right)) {
		return any_equality(p_binary);
	}
	uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
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
			// A Godot object or value against null: is there one? An object Godot has freed is not there.
			uint32_t object = expression(a == Kind::Void ? right : left, kind);
			uint32_t present = reg(Kind::Bool);
			emit(Op::HostPresent, position, present, object);
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
	} else if (!strict && ((a == Kind::I64 && (b == Kind::Bool || b == Kind::Str)) || (b == Kind::I64 && (a == Kind::Bool || a == Kind::Str)))) {
		// An Int64 against a Boolean or a String: exactly, as numbers.
		uint32_t out = reg(Kind::Bool);
		emit(Op::EqF64, position, out, wide_order(left, right, position), number_zero(position));
		if (p_binary->op == TokenKind::NotEqual) {
			uint32_t negated = reg(Kind::Bool);
			emit(Op::Not, position, negated, out);
			return negated;
		}
		return out;
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

uint32_t Lowerer::arithmetic(Binary *p_binary, Kind p_want) {
	uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
	// With an untyped side, `+` is decided as it runs: text, Godot's own `+` for Godot values, or numbers.
	if (p_binary->op == TokenKind::Plus && (untyped(left) || untyped(right))) {
		return any_add(p_binary);
	}
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

uint32_t Lowerer::binary(Binary *p_binary, Kind p_want) {
	uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
	switch (p_binary->op) {
		case TokenKind::AmpAmp:
		case TokenKind::PipePipe:
			return logical(p_binary, p_want);
		case TokenKind::Comma:
			expression(left, Kind::Void);
			return expression(right, p_want);
		case TokenKind::KwIs:
		case TokenKind::KwAs:
			return type_test(p_binary);
		case TokenKind::Less:
		case TokenKind::Greater:
		case TokenKind::LessEqual:
		case TokenKind::GreaterEqual:
			return comparison(p_binary);
		case TokenKind::Equal:
		case TokenKind::NotEqual:
		case TokenKind::StrictEqual:
		case TokenKind::StrictNotEqual:
			return equality(p_binary);
		case TokenKind::KwIn:
			return Dynamic(*this).in(p_binary);
		case TokenKind::KwInstanceof:
			refuse(position, "'instanceof'");
			return default_value(Kind::Bool, position);
		default:
			return arithmetic(p_binary, p_want);
	}
}

} // namespace ir
} // namespace kas3
