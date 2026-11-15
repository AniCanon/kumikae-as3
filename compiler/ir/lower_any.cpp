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
	// The left one is made a primitive first, whichever way the comparison goes.
	Op op = Op::AnyLt;
	if (p_binary->op == TokenKind::LessEqual) {
		op = Op::AnyLe;
	} else if (p_binary->op == TokenKind::Greater) {
		op = Op::AnyGt;
	} else if (p_binary->op == TokenKind::GreaterEqual) {
		op = Op::AnyGe;
	}
	const uint32_t a = settled(any_value(p_binary->left), p_binary->right, position);
	const uint32_t b = any_value(p_binary->right);
	uint32_t out = reg(Kind::Bool);
	emit(op, position, out, a, b);
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
	} else if (const Type *written = p_binary->right->type && p_binary->right->type->kind == Type::ClassObject ? p_binary->right->type->element : nullptr;
			is_vector(written) && vector_class(written)) {
		code = *vector_class(written); // An Array, or a Vector.<T>: of that very class.
		symbol = nullptr;
	} else if (!symbol || !class_index(symbol, &code) || !module_.classes[code].native_base.empty()) {
		return std::nullopt;
	}
	const bool test = p_binary->op == TokenKind::KwIs;
	const uint32_t value = any_value(p_binary->left);
	if (symbol) {
		touch(symbol, position); // As for a typed value: the class named is set up.
	}
	uint32_t fits = reg(Kind::Bool);
	emit(Op::AnyIs, position, fits, value, code);
	if (test) {
		return fits;
	}
	// `as`: the value as that type if it is one, otherwise null. The checker types `x as int` as an int,
	// as Adobe's compiler does, so the null is what an int holds for it: 0 (false for a Boolean).
	uint32_t out = target == Kind::F64 ? reg(Kind::F64) : default_value(target, position);
	if (target == Kind::F64) {
		emit(Op::ConstF64, position, out, number(0)); // Not the NaN a Number starts as.
	}
	const uint32_t skip = emit(Op::JumpIfNot, position, fits, 0);
	emit(Op::FromAny, position, out, value, target == Kind::Obj ? code : NONE);
	patch(skip, here());
	return out;
}

uint32_t Lowerer::empty_untyped(const Type *p_type, uint32_t p_position) {
	uint32_t out = reg(Kind::Any);
	emit(Op::AnyConst, p_position, out, starts_null_type(Kind::Any, p_type) ? 1 : 0);
	return out;
}

uint32_t Lowerer::typed(Expr *p_expression, const Type *p_type, Kind p_kind) {
	// A plain value is asked for in its own kind: a call of Godot's that gives any value then gives that one.
	const bool checked = starts_null_type(p_kind, p_type) || is_engine(p_kind) || p_kind == Kind::Obj;
	if (!checked || !untyped(p_expression)) {
		return expression(p_expression, p_kind);
	}
	return from_untyped(expression(p_expression, Kind::Any), p_type, p_kind, p_expression->offset);
}

// An untyped value stored where a type is declared.
uint32_t Lowerer::from_untyped(uint32_t p_any, const Type *p_type, Kind p_kind, uint32_t p_position) {
	if (starts_null_type(p_kind, p_type)) {
		// Into an Object: undefined becomes null, as AS3's coercion has it.
		uint32_t out = reg(Kind::Any);
		emit(Op::FromAny, p_position, out, p_any, 0);
		return out;
	}
	if (is_engine(p_kind)) {
		// Into a place of one of Godot's types: the value must be of that type, checked by its name.
		uint32_t name = NONE;
		const Symbol *godot = p_type && p_type->kind == Type::Instance ? p_type->symbol : nullptr;
		if (!godot || !is_godot(godot) || !engine_name(godot, std::string_view(), &name)) {
			name = NONE;
		}
		uint32_t out = engine_reg(p_kind, p_type);
		emit(Op::FromAny, p_position, out, p_any, name);
		return out;
	}
	if (p_kind != Kind::Obj) {
		return convert(p_any, p_kind, p_position);
	}
	const Symbol *symbol = p_type && p_type->kind == Type::Instance ? p_type->symbol : nullptr;
	uint32_t klass = NONE;
	if (is_vector(p_type)) {
		klass = vector_class(p_type).value_or(NONE);
	} else if (symbol && !class_index(symbol, &klass)) {
		klass = NONE;
	}
	if (klass == NONE) {
		refuse(p_position, "converting an untyped value to '" + compilation_.type_name(p_type) + "'");
		return default_value(Kind::Obj, p_position);
	}
	uint32_t out = reg(Kind::Obj);
	emit(Op::FromAny, p_position, out, p_any, klass);
	return out;
}

} // namespace ir
} // namespace kas3
