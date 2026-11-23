#include "lower_dynamic.h"
#include "lower_operators.h"

namespace kas3 {
namespace ir {

// `*` itself, as written: `Object` is untyped too, but starts null and reads as an object.
bool Lowerer::untyped_type(const Type *p_type) const {
	return p_type && p_type->kind == Type::Any;
}

bool Lowerer::untyped(const Expr *p_expression) const {
	Kind kind = Kind::Void;
	return p_expression && !is_null(p_expression) && value_kind(p_expression, &kind) && kind == Kind::Any;
}

// Any expression as an untyped value; `null` as null.
uint32_t Lowerer::any_value(Expr *p_expression) {
	if (is_null(p_expression)) {
		uint32_t out = reg(Kind::Any);
		emit(Op::AnyConst, p_expression->offset, out, 1);
		return out;
	}
	return expression(p_expression, Kind::Any);
}

// String(x): the words for a value, "undefined" and "null" included.
uint32_t Lowerer::any_text(Expr *p_expression) {
	uint32_t out = reg(Kind::Str);
	emit(Op::AnyText, p_expression->offset, out, any_value(p_expression));
	return out;
}

uint32_t Operators::any_equality(Binary *p_binary) {
	const uint32_t position = p_binary->offset;
	const bool strict = p_binary->op == TokenKind::StrictEqual || p_binary->op == TokenKind::StrictNotEqual;
	const bool negated = p_binary->op == TokenKind::NotEqual || p_binary->op == TokenKind::StrictNotEqual;
	const uint32_t a = l_.settled(l_.any_value(p_binary->left), p_binary->right, position);
	const uint32_t b = l_.any_value(p_binary->right);
	uint32_t out = l_.reg(Kind::Bool);
	l_.emit(strict ? Op::AnyStrictEq : Op::AnyEq, position, out, a, b);
	if (negated) {
		uint32_t flipped = l_.reg(Kind::Bool);
		l_.emit(Op::Not, position, flipped, out);
		return flipped;
	}
	return out;
}

uint32_t Operators::any_comparison(Binary *p_binary) {
	const uint32_t position = p_binary->offset;
	// The left one is made a primitive first, whichever way the comparison goes.
	Op op = Op::AnyLt;
	if (p_binary->op == TokenKind::LessEqual) {
		op = Op::AnyLe;
	} else if (p_binary->op == TokenKind::Greater) {
		op = Op::AnyGt;
	} else if (p_binary->op == TokenKind::GreaterEqual) {
		op = Op::AnyGe;
	}
	const uint32_t a = l_.settled(l_.any_value(p_binary->left), p_binary->right, position);
	const uint32_t b = l_.any_value(p_binary->right);
	uint32_t out = l_.reg(Kind::Bool);
	l_.emit(op, position, out, a, b);
	return out;
}

uint32_t Operators::any_add(Binary *p_binary) {
	const uint32_t position = p_binary->offset;
	const uint32_t a = l_.settled(l_.any_value(p_binary->left), p_binary->right, position);
	const uint32_t b = l_.any_value(p_binary->right);
	uint32_t out = l_.reg(Kind::Any);
	l_.emit(Op::AnyAdd, position, out, a, b);
	return l_.convert(out, l_.natural(p_binary), position);
}

// The operation that carries out `-`, `*`, `/` or `%` (or the same written with `=`) on untyped operands, or
// unary `-` on one; nothing for any other operator.
std::optional<Op> Operators::untyped_operator(TokenKind p_op, bool p_unary) {
	switch (p_op) {
		case TokenKind::Minus:
		case TokenKind::MinusAssign:
			return p_unary ? Op::AnyNeg : Op::AnySub;
		case TokenKind::Star:
		case TokenKind::StarAssign:
			return p_unary ? std::nullopt : std::optional<Op>(Op::AnyMul);
		case TokenKind::Slash:
		case TokenKind::SlashAssign:
			return p_unary ? std::nullopt : std::optional<Op>(Op::AnyDiv);
		case TokenKind::Percent:
		case TokenKind::PercentAssign:
			return p_unary ? std::nullopt : std::optional<Op>(Op::AnyMod);
		default:
			return std::nullopt;
	}
}

uint32_t Operators::any_operate(Op p_op, uint32_t p_left, uint32_t p_right, uint32_t p_position) {
	const uint32_t out = l_.reg(Kind::Any);
	l_.emit(p_op, p_position, out, p_left, p_op == Op::AnyNeg ? 0 : p_right);
	return out;
}

// typeof: of an untyped value, asked as the program runs; of anything else, known now.
uint32_t Lowerer::any_typeof(Expr *p_operand, uint32_t p_position) {
	uint32_t out = reg(Kind::Str);
	// Asked as it runs of an untyped value, of a String, which is "object" when it holds null, and of a
	// Function, which is "function" unless it holds null.
	const bool function = p_operand && p_operand->type && p_operand->type->kind == Type::Instance && p_operand->type->symbol == function_;
	if (untyped(p_operand) || function || (!is_null(p_operand) && natural(p_operand) == Kind::Str)) {
		emit(Op::AnyTypeof, p_position, out, any_value(p_operand));
		return out;
	}
	const Kind kind = is_null(p_operand) ? Kind::Obj : natural(p_operand);
	expression(p_operand, Kind::Void); // What it does is still done.
	const char *name = "object";
	if (is_numeric(kind)) {
		name = "number";
	} else if (kind == Kind::Bool) {
		name = "boolean";
	}
	emit(Op::ConstStr, p_position, out, string(name));
	return out;
}

// `is` and `as` asked of a value that is untyped or of a type the class can't be: what the value is now
// decides. One of Godot's objects is asked this way of a class that extends one of Godot's: the object that
// carries the script is the script's own (vm/library.h, arriving_script). Nothing if the type written is not
// one this can test.
std::optional<uint32_t> Operators::any_type_test(Binary *p_binary) {
	const uint32_t position = p_binary->offset;
	const Symbol *symbol = l_.named_class(p_binary->right);
	uint32_t code = NONE;
	Kind target = Kind::Obj;
	if (symbol == l_.int_) {
		code = ANY_IS_INT;
		target = Kind::I32;
	} else if (symbol == l_.uint_) {
		code = ANY_IS_UINT;
		target = Kind::U32;
	} else if (symbol == l_.number_) {
		code = ANY_IS_NUMBER;
		target = Kind::F64;
	} else if (symbol == l_.int64_ && l_.int64_) {
		code = ANY_IS_INT64;
		target = Kind::I64;
	} else if (symbol == l_.string_) {
		code = ANY_IS_STRING;
		target = Kind::Str;
	} else if (symbol == l_.boolean_) {
		code = ANY_IS_BOOLEAN;
		target = Kind::Bool;
	} else if (const Type *written = p_binary->right->type && p_binary->right->type->kind == Type::ClassObject ? p_binary->right->type->element : nullptr;
			l_.is_vector(written) && l_.vector_class(written)) {
		code = *l_.vector_class(written); // An Array, or a Vector.<T>: of that very class.
		symbol = nullptr;
	} else if (!symbol || !l_.class_index(symbol, &code)) {
		return std::nullopt;
	}
	const bool test = p_binary->op == TokenKind::KwIs;
	const uint32_t value = l_.any_value(p_binary->left);
	if (symbol) {
		l_.touch(symbol, position); // As for a typed value: the class named is set up.
	}
	uint32_t fits = l_.reg(Kind::Bool);
	l_.emit(Op::AnyIs, position, fits, value, code);
	if (test) {
		return fits;
	}
	// `as`: the value as that type if it is one, otherwise null. A number or a Boolean stays untyped, which
	// holds the null (Lowerer::null_as): where it is stored in an int, the null becomes 0 there.
	const bool untyped = Lowerer::is_numeric(target) || target == Kind::Bool;
	uint32_t out = untyped ? l_.reg(Kind::Any) : l_.default_value(target, position);
	if (untyped) {
		l_.emit(Op::AnyConst, position, out, 1);
	}
	const uint32_t skip = l_.emit(Op::JumpIfNot, position, fits, 0);
	if (untyped) {
		l_.emit(Op::Move, position, out, value);
	} else {
		l_.emit(Op::FromAny, position, out, value, target == Kind::Obj ? code : NONE);
	}
	l_.patch(skip, l_.here());
	return out;
}

uint32_t Lowerer::empty_untyped(const Type *p_type, uint32_t p_position) {
	uint32_t out = reg(Kind::Any);
	emit(Op::AnyConst, p_position, out, starts_null_type(Kind::Any, p_type) ? 1 : 0);
	return out;
}

uint32_t Lowerer::typed(Expr *p_expression, const Type *p_type, Kind p_kind) {
	if (p_expression && p_expression->kind == NodeKind::Member && p_kind != Kind::Any && p_kind != Kind::Void) {
		if (const auto direct = Dynamic(*this).guarded(p_expression, p_type)) {
			return *direct;
		}
	}
	if (p_expression && p_expression->kind == NodeKind::Name) {
		const Symbol *symbol = static_cast<Name *>(p_expression)->symbol;
		auto local = locals_.find(symbol);
		if (local != locals_.end() && symbol->storage && symbol->storage == p_type && kind_at(local->second) == p_kind) {
			return local->second;
		}
	}
	// A plain value is asked for in its own kind: a call of Godot's that gives any value then gives that one.
	const bool checked = starts_null_type(p_kind, p_type) || is_engine(p_kind) || p_kind == Kind::Obj;
	if (!checked || !untyped(p_expression)) {
		return expression(p_expression, p_kind);
	}
	return from_untyped(expression(p_expression, Kind::Any), p_type, p_kind, p_expression->offset);
}

// An untyped value stored where a type is declared.
uint32_t Lowerer::from_untyped(uint32_t p_any, const Type *p_type, Kind p_kind, uint32_t p_position) {
	const std::optional<uint32_t> check = check_for(p_type, p_kind);
	if (starts_null_type(p_kind, p_type) || is_engine(p_kind)) {
		// Into an Object: undefined becomes null, as AS3's coercion has it. Into a place of one of Godot's
		// types: the value must be of that type, checked by its name.
		uint32_t out = is_engine(p_kind) ? engine_reg(p_kind, p_type) : reg(Kind::Any);
		emit(Op::FromAny, p_position, out, p_any, check.value_or(NONE));
		return out;
	}
	if (p_kind != Kind::Obj) {
		return convert(p_any, p_kind, p_position);
	}
	if (!check) {
		refuse(p_position, "converting an untyped value to '" + compilation_.type_name(p_type) + "'");
		return default_value(Kind::Obj, p_position);
	}
	uint32_t out = reg(Kind::Obj);
	emit(Op::FromAny, p_position, out, p_any, *check);
	return out;
}

std::optional<uint32_t> Lowerer::class_code(const Type *p_class) {
	if (is_vector(p_class)) {
		return vector_class(p_class);
	}
	const Symbol *symbol = p_class && p_class->kind == Type::Instance ? p_class->symbol : nullptr;
	uint32_t index = NONE;
	if (symbol == int_ || symbol == uint_ || symbol == number_ || symbol == string_ || symbol == boolean_) {
		return symbol == int_ ? ANY_IS_INT : symbol == uint_ ? ANY_IS_UINT : symbol == number_ ? ANY_IS_NUMBER : symbol == string_ ? ANY_IS_STRING : ANY_IS_BOOLEAN;
	}
	if (symbol && symbol == object_) {
		Dynamic(*this).table_class(false); // What `new` of it makes.
		return ANY_IS_OBJECT;
	}
	if (symbol && is_godot(symbol)) {
		// One of Godot's classes or value types, by its name. A class of constants (Key) has none: it is no type.
		return engine_name(symbol, std::string_view(), &index) ? std::optional<uint32_t>(CLASS_GODOT | index) : std::nullopt;
	}
	if (!symbol || !class_index(symbol, &index)) {
		return std::nullopt;
	}
	return index;
}

std::optional<uint32_t> Lowerer::check_for(const Type *p_type, Kind p_kind) {
	if (starts_null_type(p_kind, p_type)) {
		return 0;
	}
	const Symbol *symbol = p_type && (p_type->kind == Type::Instance || p_type->kind == Type::Callback) ? p_type->symbol : nullptr;
	uint32_t check = NONE;
	if (is_engine(p_kind)) {
		return symbol && is_godot(symbol) && engine_name(symbol, std::string_view(), &check) ? check : NONE;
	}
	if (p_kind != Kind::Obj) {
		return NONE;
	}
	if (is_vector(p_type)) {
		return vector_class(p_type);
	}
	return symbol && class_index(symbol, &check) ? std::optional<uint32_t>(check) : std::nullopt;
}

} // namespace ir
} // namespace kas3
