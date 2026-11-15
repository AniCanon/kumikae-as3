#include "lowerer.h"

namespace kas3 {
namespace ir {

// `*` itself, as written: `Object` is untyped too, but starts null and reads as an object.
bool Lowerer::untyped_type(const Type *p_type) const {
	return p_type && p_type->kind == Type::Any;
}

bool Lowerer::untyped(const Expr *p_expression) const {
	Kind kind = Kind::Void;
	return p_expression && !is_null(p_expression) && kind_of(p_expression->type, &kind) && kind == Kind::Any;
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

uint32_t Lowerer::any_equality(Binary *p_binary) {
	const uint32_t position = p_binary->offset;
	const bool strict = p_binary->op == TokenKind::StrictEqual || p_binary->op == TokenKind::StrictNotEqual;
	const bool negated = p_binary->op == TokenKind::NotEqual || p_binary->op == TokenKind::StrictNotEqual;
	const uint32_t a = settled(any_value(p_binary->left), p_binary->right, position);
	const uint32_t b = any_value(p_binary->right);
	uint32_t out = reg(Kind::Bool);
	emit(strict ? Op::AnyStrictEq : Op::AnyEq, position, out, a, b);
	if (negated) {
		uint32_t flipped = reg(Kind::Bool);
		emit(Op::Not, position, flipped, out);
		return flipped;
	}
	return out;
}

uint32_t Lowerer::any_comparison(Binary *p_binary) {
	const uint32_t position = p_binary->offset;
	const bool swap_sides = p_binary->op == TokenKind::Greater || p_binary->op == TokenKind::GreaterEqual;
	const bool with_equal = p_binary->op == TokenKind::LessEqual || p_binary->op == TokenKind::GreaterEqual;
	const uint32_t a = settled(any_value(p_binary->left), p_binary->right, position);
	const uint32_t b = any_value(p_binary->right);
	uint32_t out = reg(Kind::Bool);
	emit(with_equal ? Op::AnyLe : Op::AnyLt, position, out, swap_sides ? b : a, swap_sides ? a : b);
	return out;
}

uint32_t Lowerer::any_add(Binary *p_binary) {
	const uint32_t position = p_binary->offset;
	const uint32_t a = settled(any_value(p_binary->left), p_binary->right, position);
	const uint32_t b = any_value(p_binary->right);
	uint32_t out = reg(Kind::Any);
	emit(Op::AnyAdd, position, out, a, b);
	return convert(out, natural(p_binary), position);
}

// typeof: of an untyped value, asked as the program runs; of anything else, known now.
uint32_t Lowerer::any_typeof(Expr *p_operand, uint32_t p_position) {
	uint32_t out = reg(Kind::Str);
	if (untyped(p_operand)) {
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
	} else if (kind == Kind::Str) {
		name = "string"; // A String that is null is still "string" to AS3's checker: typeof of a typed null.
	}
	emit(Op::ConstStr, p_position, out, string(name));
	return out;
}

// `is` and `as` asked of a value that is untyped or of a type the class can't be: what the value is now
// decides. Nothing if the type written is not one this can test.
std::optional<uint32_t> Lowerer::any_type_test(Binary *p_binary) {
	const uint32_t position = p_binary->offset;
	const Symbol *symbol = named_class(p_binary->right);
	uint32_t code = NONE;
	Kind target = Kind::Obj;
	if (symbol == int_) {
		code = ANY_IS_INT;
		target = Kind::I32;
	} else if (symbol == uint_) {
		code = ANY_IS_UINT;
		target = Kind::U32;
	} else if (symbol == number_) {
		code = ANY_IS_NUMBER;
		target = Kind::F64;
	} else if (symbol == string_) {
		code = ANY_IS_STRING;
		target = Kind::Str;
	} else if (symbol == boolean_) {
		code = ANY_IS_BOOLEAN;
		target = Kind::Bool;
	} else if (!symbol || !class_index(symbol, &code) || !module_.classes[code].native_base.empty()) {
		return std::nullopt;
	}
	const bool test = p_binary->op == TokenKind::KwIs;
	if (!test && target != Kind::Obj && target != Kind::Str) {
		return std::nullopt; // `x as int` gives a number or null: a typed register cannot hold both.
	}
	const uint32_t value = any_value(p_binary->left);
	uint32_t fits = reg(Kind::Bool);
	emit(Op::AnyIs, position, fits, value, code);
	if (test) {
		return fits;
	}
	// `as`: the value as that type if it is one, otherwise null.
	uint32_t out = default_value(target, position);
	const uint32_t skip = emit(Op::JumpIfNot, position, fits, 0);
	emit(Op::FromAny, position, out, value, target == Kind::Obj ? code : NONE);
	patch(skip, here());
	return out;
}

uint32_t Lowerer::typed(Expr *p_expression, const Type *p_type, Kind p_kind) {
	if (p_kind == Kind::Any && !untyped_type(p_type) && untyped(p_expression)) {
		// Into an Object: undefined becomes null, as AS3's coercion has it.
		uint32_t out = reg(Kind::Any);
		emit(Op::FromAny, p_expression->offset, out, expression(p_expression, Kind::Any), 0);
		return out;
	}
	if (p_kind != Kind::Obj || !untyped(p_expression)) {
		return expression(p_expression, p_kind);
	}
	const Symbol *symbol = p_type && p_type->kind == Type::Instance ? p_type->symbol : nullptr;
	uint32_t klass = NONE;
	if (is_vector(p_type)) {
		klass = vector_class(p_type).value_or(NONE);
	} else if (symbol) {
		const auto found = classes_.find(symbol);
		klass = found == classes_.end() ? NONE : found->second;
	}
	if (klass == NONE) {
		refuse(p_expression->offset, "converting an untyped value to '" + compilation_.type_name(p_type) + "'");
		return default_value(Kind::Obj, p_expression->offset);
	}
	uint32_t out = reg(Kind::Obj);
	emit(Op::FromAny, p_expression->offset, out, expression(p_expression, Kind::Any), klass);
	return out;
}

} // namespace ir
} // namespace kas3
