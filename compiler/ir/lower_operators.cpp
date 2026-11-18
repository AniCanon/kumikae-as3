#include "lower_dynamic.h"
#include "lower_engine.h"
#include "lower_library.h"
#include "lower_operators.h"

namespace kas3 {
namespace ir {

bool Lowerer::whole(const Expr *p_expression) {
	return is_whole_number(p_expression, [this](const Expr *p_operand) {
		Kind kind = Kind::Void;
		return value_kind(p_operand, &kind) && is_integral(kind);
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
uint32_t Operators::concatenate(uint32_t p_left, uint32_t p_right, uint32_t p_position) {
	uint32_t left = l_.reg(Kind::Str), right = l_.reg(Kind::Str), out = l_.reg(Kind::Str);
	l_.emit(Op::StrToStr, p_position, left, l_.convert(p_left, Kind::Str, p_position));
	l_.emit(Op::StrToStr, p_position, right, l_.convert(p_right, Kind::Str, p_position));
	l_.emit(Op::Concat, p_position, out, left, right);
	return out;
}

uint32_t Operators::operate(TokenKind p_op, uint32_t p_left, uint32_t p_right, bool p_string, uint32_t p_position) {
	if (p_string) {
		return concatenate(p_left, p_right, p_position);
	}
	const Arithmetic *how = arithmetic_of(p_op);
	if (!how) {
		l_.refuse(p_position, "this operator");
		how = &ARITHMETIC[0];
	}
	// A shift is as wide as what is shifted; its count is only ever its low bits, so an Int64 count is cut.
	const bool shift = how->narrow == Op::Shl || how->narrow == Op::Shr || how->narrow == Op::UShr;
	if (shift && l_.kind_at(p_left) != Kind::I64 && l_.kind_at(p_right) == Kind::I64) {
		p_right = l_.convert(p_right, Kind::I32, p_position, true);
	}
	if (l_.kind_at(p_left) == Kind::I64 || l_.kind_at(p_right) == Kind::I64) {
		// Anything that has no 64-bit form falls through to Numbers: the Int64 converts, and says so if
		// it does not fit.
		const bool whole = Lowerer::is_integral(l_.kind_at(p_left)) && Lowerer::is_integral(l_.kind_at(p_right));
		if (how->wide != Op::Stmt && (!how->wide_needs_whole || whole)) {
			uint32_t left = l_.convert(p_left, Kind::I64, p_position);
			uint32_t right = l_.convert(p_right, Kind::I64, p_position);
			uint32_t out = l_.reg(Kind::I64);
			l_.emit(how->wide, p_position, out, left, right);
			return out;
		}
	}
	uint32_t out = l_.reg(how->result);
	uint32_t left = l_.convert(p_left, how->narrow == Op::UShr ? Kind::U32 : how->operands, p_position);
	l_.emit(how->narrow, p_position, out, left, l_.convert(p_right, how->operands, p_position));
	return out;
}

uint32_t Operators::logical(Binary *p_binary, Kind p_want) {
	uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
	// As a condition it is a Boolean; otherwise it is one of its operands, which must then be
	// of one kind.
	Kind kind = Kind::Bool;
	if (p_want != Kind::Bool) {
		Kind a = Lowerer::is_null(left) ? Kind::Void : l_.natural(left);
		Kind b = Lowerer::is_null(right) ? Kind::Void : l_.natural(right);
		if (a != b && Lowerer::is_integral(a) && Lowerer::is_integral(b) && (a == Kind::I64 || b == Kind::I64)) {
			a = b = Kind::I64; // An Int64 holds both, as the checker took it.
		}
		kind = a == Kind::Void ? b : a;
		if (a == Kind::Any || b == Kind::Any) {
			kind = a = b = Kind::Any; // An untyped value holds either side.
		}
		if (kind == Kind::Void || (a != Kind::Void && b != Kind::Void && a != b)) {
			l_.refuse(position, "'&&' or '||' between values of different types");
			kind = Kind::Bool;
		}
	}
	uint32_t out = l_.reg(kind);
	l_.emit(Op::Move, position, out, l_.expression(left, kind));
	uint32_t test = l_.convert(out, Kind::Bool, position);
	uint32_t skip = l_.emit(p_binary->op == TokenKind::AmpAmp ? Op::JumpIfNot : Op::JumpIf, position, test, 0);
	l_.emit(Op::Move, position, out, l_.expression(right, kind));
	l_.patch(skip, l_.here());
	return out;
}

// The Godot object a script object is attached to, for `is` and `as` with one of Godot's classes. A
// script class that extends a Godot class holds it in its first field; an object of any other class of
// the program is no Godot object, and nor is no object at all: both are null.
uint32_t Operators::godot_object_of(uint32_t p_object, Expr *p_written, uint32_t p_position) {
	const Symbol *symbol = p_written->type && p_written->type->kind == Type::Instance ? p_written->type->symbol : nullptr;
	const auto found = symbol ? l_.classes_.find(symbol) : l_.classes_.end();
	const uint32_t owner = l_.default_value(Kind::Ref, p_position);
	if (found == l_.classes_.end() || l_.module_.classes[found->second].native_base.empty()) {
		return owner;
	}
	const uint32_t skip = l_.emit(Op::JumpIfNot, p_position, l_.convert(p_object, Kind::Bool, p_position), 0);
	l_.emit(Op::GetField, p_position, owner, p_object, 0);
	l_.patch(skip, l_.here());
	return owner;
}

// An untyped value and one of Godot's classes: whether it holds such an object decides.
uint32_t Operators::untyped_godot_test(Binary *p_binary, uint32_t p_godot_class) {
	const uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	const bool test = p_binary->op == TokenKind::KwIs;
	const uint32_t value = l_.any_value(left);
	uint32_t fits = l_.reg(Kind::Bool);
	l_.emit(Op::AnyIsGodot, position, fits, value, p_godot_class);
	if (test) {
		return fits;
	}
	uint32_t out = l_.engine_reg(Kind::Ref, p_binary->type);
	l_.emit(Op::ConstNull, position, out);
	const uint32_t skip = l_.emit(Op::JumpIfNot, position, fits, 0);
	l_.emit(Op::FromAny, position, out, value, p_godot_class);
	l_.patch(skip, l_.here());
	return out;
}

// One of Godot's classes: the engine knows what an object of its own is.
uint32_t Operators::godot_test(Binary *p_binary, uint32_t p_godot_class) {
	const uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	const bool test = p_binary->op == TokenKind::KwIs;
	uint32_t object = l_.expression(left, Kind::Void);
	if (l_.kind_at(object) == Kind::Obj) {
		object = godot_object_of(object, left, position);
	}
	if (l_.kind_at(object) != Kind::Ref) {
		if (!test || l_.kind_at(object) != Kind::Str) {
			l_.refuse(left->offset, std::string(test ? "'is'" : "'as'") + " with a Godot class on a value that is not one of Godot's objects");
			return l_.default_value(test ? Kind::Bool : Kind::Ref, position);
		}
		return l_.default_value(Kind::Bool, position); // A string is never one of Godot's objects.
	}
	uint32_t out = test ? l_.reg(Kind::Bool) : l_.engine_reg(Kind::Ref, p_binary->type);
	l_.emit(test ? Op::HostIsClass : Op::HostAsClass, position, out, object, p_godot_class);
	return out;
}

// `x instanceof C`: whether C is in the chain of prototypes of x. For a class named, that is `is`, but
// for an interface, which is in no chain, and for int and uint: a number's prototype is Number's
// (avmplus's Toplevel::instanceof, from recollection).
uint32_t Operators::instance_of(Binary *p_binary) {
	const Symbol *symbol = l_.named_class(p_binary->right);
	if (!symbol && !(p_binary->right->type && p_binary->right->type->kind == Type::ClassObject)) {
		return class_test(p_binary, Op::ClassInstanceOf);
	}
	if (symbol && !Lowerer::is_godot(symbol) && l_.untyped_type(p_binary->left->type)) {
		// What may hold undefined is asked as it runs: undefined has no chain to look in (Error #1010).
		return class_test(p_binary, Op::ClassInstanceOf);
	}
	if (symbol && (symbol->kind == SymbolKind::Interface || symbol == l_.int_ || symbol == l_.uint_)) {
		l_.expression(p_binary->left, Kind::Void);
		return l_.default_value(Kind::Bool, p_binary->offset);
	}
	Binary test = *p_binary;
	test.op = TokenKind::KwIs;
	return type_test(&test);
}

// `x is c`, `x as c`, `x instanceof c` where c is no class named but a value that holds one.
uint32_t Operators::class_test(Binary *p_binary, Op p_op) {
	const uint32_t position = p_binary->offset;
	const uint32_t value = l_.settled(l_.any_value(p_binary->left), p_binary->right, position);
	const uint32_t klass = l_.any_value(p_binary->right);
	const uint32_t out = l_.reg(p_op == Op::ClassAs ? Kind::Any : Kind::Bool);
	l_.emit(p_op, position, out, value, klass);
	return out;
}

uint32_t Operators::type_test(Binary *p_binary) {
	uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
	Symbol *symbol = l_.named_class(right);
	if (!symbol && !(right->type && right->type->kind == Type::ClassObject)) {
		return class_test(p_binary, p_binary->op == TokenKind::KwIs ? Op::ClassIs : Op::ClassAs);
	}
	uint32_t index = 0;
	// `v is Vector.<int>`: the type written is the class itself, and its instances are the vector.
	const Type *written = right->type && right->type->kind == Type::ClassObject ? right->type->element : nullptr;
	uint32_t godot_class = 0;
	const bool godot_target = symbol && Lowerer::is_godot(symbol) && l_.is_godot_object(symbol);
	// A number, a Boolean or a String is asked of the value as it runs, whatever is on the left: an object
	// or `null` is none, and gives false or null like any other value that is not one.
	const bool plain = symbol && (symbol == l_.int_ || symbol == l_.uint_ || symbol == l_.number_ || symbol == l_.boolean_ || symbol == l_.string_);
	if (!godot_target && (l_.untyped(left) || plain || (!l_.is_vector(written) && !Lowerer::is_null(left) && l_.natural(left) != Kind::Obj))) {
		if (const std::optional<uint32_t> tested = any_type_test(p_binary)) {
			return *tested;
		}
	}
	if (godot_target && l_.untyped(left) && l_.engine_name(symbol, std::string_view(), &godot_class)) {
		return untyped_godot_test(p_binary, godot_class);
	}
	if (godot_target && l_.engine_name(symbol, std::string_view(), &godot_class)) {
		return godot_test(p_binary, godot_class);
	}
	if (const std::optional<uint32_t> of_vector = l_.is_vector(written) ? l_.vector_class(written) : std::nullopt) {
		index = *of_vector;
	} else if (symbol == l_.object_ && p_binary->op == TokenKind::KwIs && l_.natural(left) == Kind::Obj) {
		// Every object is an Object: only null is not.
		uint32_t out = l_.reg(Kind::Bool);
		l_.emit(Op::ObjToBool, position, out, l_.expression(left, Kind::Obj));
		return out;
	} else if (symbol == l_.object_ && l_.object_) {
		// Any value, whatever its type: everything but null and undefined is an Object, and stays what it is.
		const uint32_t value = l_.any_value(left);
		const uint32_t null = l_.reg(Kind::Any);
		l_.emit(Op::AnyConst, position, null, 1);
		if (p_binary->op == TokenKind::KwIs) {
			const uint32_t missing = l_.reg(Kind::Bool);
			l_.emit(Op::AnyEq, position, missing, value, null);
			const uint32_t out = l_.reg(Kind::Bool);
			l_.emit(Op::Not, position, out, missing);
			return out;
		}
		const uint32_t out = l_.reg(Kind::Any);
		l_.emit(Op::FromAny, position, out, value, 0);
		return out;
	} else if (!symbol || !l_.class_index(symbol, &index)) {
		l_.refuse(right->offset, std::string(p_binary->op == TokenKind::KwIs ? "'is'" : "'as'") + " with this type");
		return l_.default_value(p_binary->op == TokenKind::KwIs ? Kind::Bool : Kind::Obj, position);
	}
	uint32_t object = l_.expression(left, Kind::Void);
	if (l_.kind_at(object) != Kind::Obj) {
		l_.refuse(left->offset, std::string(p_binary->op == TokenKind::KwIs ? "'is'" : "'as'") + " on a value that is not an object");
		return l_.default_value(Kind::Bool, position);
	}
	if (symbol) {
		l_.touch(symbol, position); // The class named is looked up after the value, and set up then, as in AVM2.
	}
	uint32_t out = l_.reg(p_binary->op == TokenKind::KwIs ? Kind::Bool : Kind::Obj);
	l_.emit(p_binary->op == TokenKind::KwIs ? Op::IsClass : Op::AsClass, position, out, object, index);
	return out;
}

uint32_t Operators::arithmetic(Binary *p_binary, Kind p_want) {
	uint32_t position = p_binary->offset;
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
	// With an untyped side, `+` is decided as it runs: text, Godot's own `+` for Godot values, or numbers.
	if (p_binary->op == TokenKind::Plus && (l_.untyped(left) || l_.untyped(right))) {
		return any_add(p_binary);
	}
	// So are `-`, `*`, `/` and `%`: Godot's own for Godot values, as typed code has them, or numbers.
	if (const std::optional<Op> op = !Lowerer::is_numeric(p_want) && (l_.untyped(left) || l_.untyped(right)) ? untyped_operator(p_binary->op, false) : std::nullopt) {
		const uint32_t a = l_.settled(l_.any_value(left), right, position);
		const uint32_t b = l_.any_value(right);
		return l_.convert(any_operate(*op, a, b, position), l_.natural(p_binary), position);
	}
	Kind left_kind = Kind::Void, right_kind = Kind::Void;
	l_.value_kind(left, &left_kind);
	l_.value_kind(right, &right_kind);
	bool strings = p_binary->op == TokenKind::Plus && l_.is_string_type(p_binary->type);
	// An object beside `+` is what ECMAScript's ToPrimitive gives with no hint: its class's own valueOf(),
	// or else its text (a Date's too), decided as it runs. Beside a String, one with no valueOf() of its
	// own is its text, written here.
	if (p_binary->op == TokenKind::Plus && (left_kind == Kind::Obj || right_kind == Kind::Obj) &&
			(!strings || Library::own_value_of(left) || Library::own_value_of(right))) {
		return any_add(p_binary);
	}
	if (!strings && (is_engine(left_kind) || is_engine(right_kind))) {
		Kind result = Kind::Void;
		if (!l_.kind_of(p_binary->type, &result) || result == Kind::Void) {
			l_.refuse(position, "this operator on a Godot value");
			return l_.default_value(p_want, position);
		}
		uint32_t a = l_.settled(Engine(l_).operand(left, right->type), right, position);
		uint32_t b = Engine(l_).operand(right, left->type);
		return Engine(l_).engine_operate(p_binary->op, { a, b }, result, p_binary->type, position);
	}
	// Division is done in Numbers, so a literal beside an Int64 stays the Number it is written as.
	bool exact = !strings && p_binary->op != TokenKind::Slash;
	uint32_t a = l_.settled(l_.wide_operand(left, exact ? right_kind : Kind::Void), right, position);
	uint32_t b = l_.wide_operand(right, exact ? left_kind : Kind::Void);
	return operate(p_binary->op, a, b, strings, position);
}

uint32_t Operators::binary(Binary *p_binary, Kind p_want) {
	Expr *left = p_binary->left;
	Expr *right = p_binary->right;
	switch (p_binary->op) {
		case TokenKind::AmpAmp:
		case TokenKind::PipePipe:
			return logical(p_binary, p_want);
		case TokenKind::Comma:
			l_.expression(left, Kind::Void);
			return l_.expression(right, p_want);
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
			return Dynamic(l_).in(p_binary);
		case TokenKind::KwInstanceof:
			return instance_of(p_binary);
		default:
			return arithmetic(p_binary, p_want);
	}
}

} // namespace ir
} // namespace kas3
