#include "lowerer.h"

namespace kas3 {
namespace ir {

std::optional<int64_t> Lowerer::wide_literal(const Expr *p_expression) {
	const WideLiteral literal = read_wide_literal(p_expression);
	return literal.state == WideLiteral::State::Fits ? std::optional<int64_t>(literal.value) : std::nullopt;
}

bool Lowerer::is_null(const Expr *p_expression) {
	return p_expression->kind == NodeKind::NullLit;
}

uint32_t Lowerer::expression(Expr *p_expression, Kind p_want) {
	if (!p_expression) {
		return default_value(p_want, 0);
	}
	if (is_null(p_expression) && p_want == Kind::Any) {
		uint32_t out = reg(Kind::Any);
		emit(Op::AnyConst, p_expression->offset, out, 1);
		return out;
	}
	if (is_null(p_expression)) {
		// null is whatever is asked for: no reference, 0, or false.
		uint32_t out = default_value(p_want == Kind::Void ? Kind::Obj : p_want == Kind::F64 ? Kind::I32 : p_want, p_expression->offset);
		return p_want == Kind::F64 ? convert(out, Kind::F64, p_expression->offset) : out;
	}
	uint32_t value = evaluate(p_expression, p_want);
	return convert(value, p_want, p_expression->offset);
}

double Lowerer::literal_value(const NumberLit *p_literal) {
	std::string text(p_literal->text);
	if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
		double value = 0;
		for (size_t i = 2; i < text.size(); i++) {
			char c = text[i];
			int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0;
			value = value * 16 + digit;
		}
		return value;
	}
	return std::strtod(text.c_str(), nullptr);
}

std::string Lowerer::unescape(std::string_view p_text) {
	std::string out;
	if (p_text.size() < 2) {
		return out;
	}
	auto hex = [&](size_t p_at, size_t p_count, uint32_t *r_value) {
		uint32_t value = 0;
		if (p_at + p_count > p_text.size() - 1) {
			return false;
		}
		for (size_t i = 0; i < p_count; i++) {
			char c = p_text[p_at + i];
			int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
			if (digit < 0) {
				return false;
			}
			value = value * 16 + (uint32_t)digit;
		}
		*r_value = value;
		return true;
	};
	auto utf8 = [&](uint32_t p_code) {
		if (p_code < 0x80) {
			out += (char)p_code;
		} else if (p_code < 0x800) {
			out += (char)(0xC0 | (p_code >> 6));
			out += (char)(0x80 | (p_code & 0x3F));
		} else if (p_code < 0x10000) {
			out += (char)(0xE0 | (p_code >> 12));
			out += (char)(0x80 | ((p_code >> 6) & 0x3F));
			out += (char)(0x80 | (p_code & 0x3F));
		} else {
			out += (char)(0xF0 | (p_code >> 18));
			out += (char)(0x80 | ((p_code >> 12) & 0x3F));
			out += (char)(0x80 | ((p_code >> 6) & 0x3F));
			out += (char)(0x80 | (p_code & 0x3F));
		}
	};
	for (size_t i = 1; i + 1 < p_text.size(); i++) {
		char c = p_text[i];
		if (c != '\\' || i + 2 >= p_text.size()) {
			out += c;
			continue;
		}
		char escape = p_text[++i];
		uint32_t code = 0;
		switch (escape) {
			case 'n':
				out += '\n';
				break;
			case 't':
				out += '\t';
				break;
			case 'r':
				out += '\r';
				break;
			case 'b':
				out += '\b';
				break;
			case 'f':
				out += '\f';
				break;
			case '0':
				out += '\0';
				break;
			case 'x':
				if (hex(i + 1, 2, &code)) {
					utf8(code);
					i += 2;
				} else {
					out += 'x';
				}
				break;
			case 'u':
				if (hex(i + 1, 4, &code)) {
					i += 4;
					// A high half written just before a low half is one character, held as its four bytes, as the
					// runtime holds it (vm/text_units.h); a half with no partner is held alone.
					uint32_t low = 0;
					if (code >= 0xD800 && code < 0xDC00 && i + 2 < p_text.size() && p_text[i + 1] == '\\' && p_text[i + 2] == 'u' &&
							hex(i + 3, 4, &low) && low >= 0xDC00 && low < 0xE000) {
						code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
						i += 6;
					}
					utf8(code);
				} else {
					out += 'u';
				}
				break;
			default:
				out += escape; // Quotes, the backslash, and anything else stand for themselves.
				break;
		}
	}
	return out;
}

bool Lowerer::builtin_constant(const Symbol *p_symbol, uint32_t p_position, uint32_t *r_reg) {
	if (!p_symbol || !p_symbol->unit || !p_symbol->unit->is_builtin ||
			(p_symbol->kind != SymbolKind::Variable && p_symbol->kind != SymbolKind::Field)) {
		return false;
	}
	const Symbol *owner = p_symbol->owner;
	std::string_view name = p_symbol->name;
	const double infinity = std::numeric_limits<double>::infinity();
	const double nan = std::numeric_limits<double>::quiet_NaN();
	if (!owner && name == "undefined") {
		*r_reg = reg(Kind::Any);
		emit(Op::AnyConst, p_position, *r_reg, 0);
		return true;
	}
	if (!owner || owner == number_) {
		double value = 0;
		if (name == "NaN") {
			value = nan;
		} else if ((!owner && name == "Infinity") || (owner && name == "POSITIVE_INFINITY")) {
			value = infinity;
		} else if (owner && name == "NEGATIVE_INFINITY") {
			value = -infinity;
		} else if (owner && name == "MAX_VALUE") {
			value = std::numeric_limits<double>::max();
		} else if (owner && name == "MIN_VALUE") {
			value = std::numeric_limits<double>::denorm_min(); // The smallest above zero, as in AS3.
		} else {
			return false;
		}
		*r_reg = reg(Kind::F64);
		emit(Op::ConstF64, p_position, *r_reg, number(value));
		return true;
	}
	if (const std::optional<double> constant = math_constant(p_symbol)) {
		*r_reg = reg(Kind::F64);
		emit(Op::ConstF64, p_position, *r_reg, number(*constant));
		return true;
	}
	if (owner == int_ && (name == "MAX_VALUE" || name == "MIN_VALUE")) {
		*r_reg = reg(Kind::I32);
		emit(Op::ConstI32, p_position, *r_reg, name == "MAX_VALUE" ? 0x7FFFFFFFu : 0x80000000u);
		return true;
	}
	if (owner == int64_ && (name == "MAX_VALUE" || name == "MIN_VALUE")) {
		*r_reg = reg(Kind::I64);
		emit(Op::ConstI64, p_position, *r_reg, wide(name == "MAX_VALUE" ? INT64_MAX : INT64_MIN));
		return true;
	}
	if (owner == array_ && array_) {
		// Array's sort options.
		constexpr std::pair<std::string_view, uint32_t> options[] = { { "CASEINSENSITIVE", 1 }, { "DESCENDING", 2 }, { "UNIQUESORT", 4 },
			{ "RETURNINDEXEDARRAY", 8 }, { "NUMERIC", 16 } };
		for (const auto &[option, value] : options) {
			if (name == option) {
				*r_reg = reg(Kind::U32);
				emit(Op::ConstU32, p_position, *r_reg, value);
				return true;
			}
		}
	}
	if (owner == uint_ && (name == "MAX_VALUE" || name == "MIN_VALUE")) {
		*r_reg = reg(Kind::U32);
		emit(Op::ConstU32, p_position, *r_reg, name == "MAX_VALUE" ? 0xFFFFFFFFu : 0u);
		return true;
	}
	return false;
}

Symbol *Lowerer::callee_symbol(Expr *p_callee) {
	if (p_callee->kind == NodeKind::Name) {
		return static_cast<Name *>(p_callee)->symbol;
	}
	if (p_callee->kind == NodeKind::Member) {
		Member *member = static_cast<Member *>(p_callee);
		return member->resolved();
	}
	return nullptr;
}

Symbol *Lowerer::named_class(Expr *p_expression) {
	Symbol *symbol = callee_symbol(p_expression);
	return symbol && symbol->is_type() ? symbol : nullptr;
}

uint32_t Lowerer::number_literal(Expr *p_expression, Kind p_want) {
	uint32_t position = p_expression->offset;
	if (const std::optional<int64_t> exact = wide_literal(p_expression); exact && p_want == Kind::I64) {
		// Digit by digit: a Number would lose the last digits of a large one.
		uint32_t out = reg(Kind::I64);
		emit(Op::ConstI64, position, out, wide(*exact));
		return out;
	}
	return number_constant(literal_value(static_cast<NumberLit *>(p_expression)), natural(p_expression), p_want, position);
}

// A number known now, as one constant: in the kind that is wanted when it fits that exactly, else its own.
uint32_t Lowerer::number_constant(double p_value, Kind p_kind, Kind p_want, uint32_t p_position) {
	const double value = p_value;
	const uint32_t position = p_position;
	Kind kind = p_kind;
	// A literal is written straight into the kind that is wanted, when it fits exactly.
	if ((p_want == Kind::F64 || p_want == Kind::I32 || p_want == Kind::U32) && kind != p_want) {
		bool fits = p_want == Kind::F64 || (value == std::floor(value) && (p_want == Kind::I32 ? value >= -2147483648.0 && value <= 2147483647.0 : value >= 0 && value <= 4294967295.0));
		kind = fits ? p_want : kind;
	}
	uint32_t out = reg(kind);
	if (kind == Kind::I32) {
		emit(Op::ConstI32, position, out, (uint32_t)(int32_t)value);
	} else if (kind == Kind::U32) {
		emit(Op::ConstU32, position, out, (uint32_t)value);
	} else {
		emit(Op::ConstF64, position, out, number(value));
	}
	return out;
}

uint32_t Lowerer::name_value(Expr *p_expression) {
	uint32_t position = p_expression->offset;
	Name *name = static_cast<Name *>(p_expression);
	uint32_t constant = 0;
	if (builtin_constant(name->symbol, position, &constant)) {
		return constant;
	}
	if (name->symbol && (name->symbol->kind == SymbolKind::Method || name->symbol->kind == SymbolKind::Function)) {
		return function_value(p_expression); // A method or a function as a value, not called.
	}
	natural(p_expression);
	return load(place_of(name), position);
}

uint32_t Lowerer::member_value(Expr *p_expression) {
	uint32_t position = p_expression->offset;
	Member *member = static_cast<Member *>(p_expression);
	Symbol *symbol = member->resolved();
	// The one member of a built-in value so far: a string's length.
	if (symbol && symbol->owner == string_ && symbol->name == "length") {
		uint32_t text = expression(member->object, Kind::Str);
		emit(Op::NullCheck, member->object->offset, text);
		uint32_t out = reg(Kind::I32);
		emit(Op::StrLength, position, out, text);
		return out;
	}
	if ((!symbol || symbol->owner == signal_) && signal_ && signal_->klass && member->object->kind == NodeKind::Index) {
		// `node[name].numListeners`: a signal's property, the signal found by its name as the line runs.
		const auto property = signal_->klass->members.find(member->name);
		uint32_t getter = 0, object = 0, name = 0;
		Kind kind = Kind::Void;
		const Symbol *read = property != signal_->klass->members.end() && property->second->kind == SymbolKind::Property ? property->second : nullptr;
		TypeRef *written = read ? (read->type ? read->type : read->getter ? read->getter->return_type : nullptr) : nullptr;
		if (written && kind_of(compilation_.type_of(written), &kind) && engine_name(signal_, member->name, &getter)) {
			const uint32_t found = signal_by_name(member->object, nullptr, &object, &name);
			if (found != NONE) {
				emit(Op::HostMember, member->object->offset, found, object, name);
				const uint32_t out = reg(kind);
				emit(Op::HostGet, position, out, found, getter);
				return out;
			}
		}
	}
	if (symbol && function_ && symbol->owner == function_ && symbol->name == "length") {
		const uint32_t value = expression(member->object, Kind::Obj);
		const uint32_t out = reg(Kind::I32);
		emit(Op::FunctionLength, position, out, value);
		return out;
	}
	uint32_t constant = 0;
	if (builtin_constant(symbol, position, &constant)) {
		return constant;
	}
	if (symbol && (symbol->kind == SymbolKind::Method || symbol->kind == SymbolKind::Function)) {
		return function_value(p_expression); // A method or a function as a value, not called.
	}
	natural(p_expression);
	return load(place_of(member), member->name_offset);
}

uint32_t Lowerer::new_value(Expr *p_expression) {
	uint32_t position = p_expression->offset;
	New *made = static_cast<New *>(p_expression);
	if (is_vector(made->type)) {
		return vector_new(made->type, made->arguments, made->callee->offset);
	}
	Symbol *symbol = named_class(made->callee);
	uint32_t index = 0;
	uint32_t type_name = 0;
	if (symbol && is_godot(symbol) && !is_godot_object(symbol) && engine_name(symbol, std::string_view(), &type_name)) {
		// One of Godot's value types: the engine makes it.
		Symbol *constructor = symbol->klass ? symbol->klass->constructor : nullptr;
		FunctionDecl *decl = constructor && constructor->node && constructor->node->kind == NodeKind::FunctionDecl
				? static_cast<FunctionDecl *>(constructor->node)
				: nullptr;
		std::vector<uint32_t> list;
		engine_arguments(decl, made->arguments, position, list);
		if (stores_int32(made->type)) {
			// Its constructor takes anything in its first places, so the width is checked here.
			for (size_t i = 0; i < list.size() && i < made->arguments.count; i++) {
				if (is_integral(kind_at(list[i])) || whole(made->arguments[i])) {
					list[i] = as_int32(list[i], made->arguments[i]->offset);
				}
			}
		}
		uint32_t value = reg(Kind::Val);
		emit(Op::HostNew, position, value, type_name, arguments(std::move(list)));
		return value;
	}
	if (symbol && is_godot(symbol) && is_godot_object(symbol) && made->arguments.count == 0 && engine_name(symbol, std::string_view(), &type_name)) {
		// One of Godot's own objects: the engine makes it, as GDScript's `Node2D.new()` does. A node is
		// then the program's to put in the tree or free; a counted object goes when nothing holds it.
		uint32_t value = engine_reg(Kind::Ref, made->type);
		emit(Op::HostNew, position, value, type_name, arguments({}));
		return value;
	}
	if (!symbol || !class_index(symbol, &index)) {
		refuse(made->callee->offset, symbol ? "creating a '" + std::string(symbol->name) + "'" : std::string("this 'new'"));
		return default_value(Kind::Obj, position);
	}
	touch(symbol, position);
	uint32_t out = reg(Kind::Obj);
	emit(Op::New, position, out, index);
	std::vector<uint32_t> list = { out };
	constructor_arguments(symbol, made->arguments, position, list);
	emit(Op::Call, position, NONE, module_.classes[index].constructor, arguments(std::move(list)));
	return out;
}

// Whether a class extends Godot's RefCounted, by its own bases.
bool Lowerer::extends_counted(const Symbol *p_class) const {
	for (const Symbol *at = p_class; at && at->klass; at = at->klass->base) {
		if (at->name == "RefCounted" && is_godot(at)) {
			return true;
		}
	}
	return false;
}

uint32_t Lowerer::postfix(Expr *p_expression) {
	uint32_t position = p_expression->offset;
	Postfix *postfix = static_cast<Postfix *>(p_expression);
	Place place = place_of(postfix->operand);
	if (place.kind == Kind::I64) {
		uint32_t old = reg(Kind::I64);
		emit(Op::Move, position, old, load(place, position));
		uint32_t one = reg(Kind::I64);
		emit(Op::ConstI64, position, one, wide(1));
		uint32_t next = reg(Kind::I64);
		emit(postfix->op == TokenKind::PlusPlus ? Op::AddI64 : Op::SubI64, position, next, old, one);
		store(place, next, position);
		return old;
	}
	// The value is the old one, as a number.
	uint32_t old = convert(load(place, position), Kind::F64, position);
	if (old == place.index && place.where == Place::Register) {
		uint32_t copy = reg(Kind::F64);
		emit(Op::Move, position, copy, old);
		old = copy;
	}
	uint32_t one = reg(Kind::F64);
	emit(Op::ConstF64, position, one, number(1));
	uint32_t next = reg(Kind::F64);
	emit(postfix->op == TokenKind::PlusPlus ? Op::AddF64 : Op::SubF64, position, next, old, one);
	store(place, convert(next, place.kind, position), position);
	return old;
}

uint32_t Lowerer::conditional(Expr *p_expression, Kind p_want) {
	uint32_t position = p_expression->offset;
	Conditional *conditional = static_cast<Conditional *>(p_expression);
	Kind kind = p_want != Kind::Void ? p_want : natural(p_expression);
	uint32_t out = reg(kind);
	uint32_t test = expression(conditional->condition, Kind::Bool);
	uint32_t to_else = emit(Op::JumpIfNot, position, test, 0);
	uint32_t then_value = expression(conditional->then_value, kind);
	emit(Op::Move, position, out, then_value);
	uint32_t to_end = emit(Op::Jump, position, 0);
	patch(to_else, here());
	uint32_t else_value = expression(conditional->else_value, kind);
	emit(Op::Move, position, out, else_value);
	patch(to_end, here());
	return out;
}

uint32_t Lowerer::evaluate(Expr *p_expression, Kind p_want) {
	uint32_t position = p_expression->offset;
	if (const auto held = evaluated_.find(p_expression); held != evaluated_.end()) {
		return held->second;
	}
	switch (p_expression->kind) {
		case NodeKind::NumberLit:
			return number_literal(p_expression, p_want);
		case NodeKind::StringLit: {
			uint32_t out = reg(Kind::Str);
			emit(Op::ConstStr, position, out, string(unescape(static_cast<StringLit *>(p_expression)->text)));
			return out;
		}
		case NodeKind::BoolLit: {
			uint32_t out = reg(Kind::Bool);
			emit(Op::ConstBool, position, out, static_cast<BoolLit *>(p_expression)->value ? 1 : 0);
			return out;
		}
		case NodeKind::ThisExpr:
			if (received_env_) {
				// In a function expression AS3's `this` is not the object the function was written in.
				refuse(position, "'this' in a function expression");
				return default_value(Kind::Obj, position);
			}
			if (fn().is_static) {
				refuse(position, "'this' here");
				return default_value(Kind::Obj, position);
			}
			return 0;
		case NodeKind::Name:
			return name_value(p_expression);
		case NodeKind::Member:
			return member_value(p_expression);
		case NodeKind::Call:
			return call(static_cast<Call *>(p_expression), p_want);
		case NodeKind::New:
			return new_value(p_expression);
		case NodeKind::Unary:
			return unary(static_cast<Unary *>(p_expression), p_want);
		case NodeKind::Postfix:
			return postfix(p_expression);
		case NodeKind::Binary:
			return binary(static_cast<Binary *>(p_expression), p_want);
		case NodeKind::Assign:
			return assign(static_cast<Assign *>(p_expression));
		case NodeKind::Conditional:
			return conditional(p_expression, p_want);
		case NodeKind::SuperExpr:
			refuse(position, "'super' used as a value");
			break;
		case NodeKind::RegexLit:
			refuse(position, "a regular expression");
			break;
		case NodeKind::ArrayLit:
			return vector_literal(p_expression, static_cast<ArrayLit *>(p_expression)->elements);
		case NodeKind::ObjectLit:
			refuse(position, "an Object literal");
			break;
		case NodeKind::VectorLit:
			return vector_literal(p_expression, static_cast<VectorLit *>(p_expression)->elements);
		case NodeKind::FunctionExpr:
			return closure_value(static_cast<FunctionExpr *>(p_expression));
		case NodeKind::Index:
			return load(place_of(p_expression), position);
		case NodeKind::TypeApply:
			refuse(position, "a Vector");
			break;
		default:
			refuse(position, "this expression");
			break;
	}
	return default_value(p_want, position);
}

uint32_t Lowerer::unary(Unary *p_unary, Kind p_want) {
	uint32_t position = p_unary->offset;
	switch (p_unary->op) {
		case TokenKind::Minus:
		case TokenKind::Plus: {
			Kind result = Kind::Void;
			if (is_engine(natural(p_unary->operand)) && kind_of(p_unary->type, &result) && result != Kind::Void) {
				return engine_operate(p_unary->op, { operand(p_unary->operand) }, result, p_unary->type, position);
			}
			if (const std::optional<int64_t> exact = wide_literal(p_unary); exact && p_want == Kind::I64) {
				uint32_t out = reg(Kind::I64);
				emit(Op::ConstI64, position, out, wide(*exact)); // The least Int64 has no positive to negate.
				return out;
			}
			if (kind_of(p_unary->operand->type, &result) && result == Kind::I64) {
				uint32_t value = expression(p_unary->operand, Kind::I64);
				if (p_unary->op == TokenKind::Plus) {
					return value;
				}
				uint32_t out = reg(Kind::I64);
				emit(Op::NegI64, position, out, value);
				return out;
			}
			if (p_unary->op == TokenKind::Plus) {
				return expression(p_unary->operand, Kind::F64);
			}
			if (p_unary->operand->kind == NodeKind::NumberLit) {
				// `-1` is one constant, not a 1 negated as the program runs. -0 stays a Number.
				const double value = -literal_value(static_cast<NumberLit *>(p_unary->operand));
				return number_constant(value, natural(p_unary), value == 0 ? Kind::F64 : p_want, position);
			}
			uint32_t out = reg(Kind::F64);
			emit(Op::NegF64, position, out, expression(p_unary->operand, Kind::F64));
			return out;
		}
		case TokenKind::KwTypeof:
			return any_typeof(p_unary->operand, position);
		case TokenKind::Bang: {
			uint32_t out = reg(Kind::Bool);
			emit(Op::Not, position, out, expression(p_unary->operand, Kind::Bool));
			return out;
		}
		case TokenKind::Tilde: {
			Kind operand_kind = Kind::Void;
			if (kind_of(p_unary->operand->type, &operand_kind) && operand_kind == Kind::I64) {
				uint32_t out = reg(Kind::I64);
				emit(Op::BitNotI64, position, out, expression(p_unary->operand, Kind::I64));
				return out;
			}
			uint32_t out = reg(Kind::I32);
			emit(Op::BitNot, position, out, expression(p_unary->operand, Kind::I32));
			return out;
		}
		case TokenKind::PlusPlus:
		case TokenKind::MinusMinus: {
			Place place = place_of(p_unary->operand);
			if (place.kind == Kind::I64) {
				uint32_t one = reg(Kind::I64);
				emit(Op::ConstI64, position, one, wide(1));
				uint32_t next = reg(Kind::I64);
				emit(p_unary->op == TokenKind::PlusPlus ? Op::AddI64 : Op::SubI64, position, next, load(place, position), one);
				store(place, next, position);
				return next;
			}
			uint32_t old = convert(load(place, position), Kind::F64, position);
			uint32_t one = reg(Kind::F64);
			emit(Op::ConstF64, position, one, number(1));
			uint32_t next = reg(Kind::F64);
			emit(p_unary->op == TokenKind::PlusPlus ? Op::AddF64 : Op::SubF64, position, next, old, one);
			uint32_t stored = convert(next, place.kind, position);
			store(place, stored, position);
			return stored;
		}
		default:
			refuse(position, "this operator");
			return default_value(Kind::F64, position);
	}
}

uint32_t Lowerer::wide_operand(Expr *p_expression, Kind p_beside) {
	if (const std::optional<int64_t> exact = wide_literal(p_expression); exact && p_beside == Kind::I64) {
		uint32_t out = reg(Kind::I64);
		emit(Op::ConstI64, p_expression->offset, out, wide(*exact));
		return out;
	}
	return expression(p_expression, Kind::Void);
}

Kind Lowerer::operand_kind(Expr *p_expression, Expr *p_other) {
	Kind kind = Kind::Void, other = Kind::Void;
	kind_of(p_expression->type, &kind);
	if (kind_of(p_other->type, &other) && other == Kind::I64 && wide_literal(p_expression)) {
		return Kind::I64;
	}
	return kind;
}

uint32_t Lowerer::assign(Assign *p_assign) {
	uint32_t position = p_assign->offset;
	Place place = place_of(p_assign->target, p_assign->value);
	uint32_t value = 0;
	if (p_assign->op == TokenKind::Assign) {
		const std::optional<uint32_t> collection = place.where == Place::Engine ? vector_to_engine(p_assign->value, place.type) : std::nullopt;
		value = collection ? *collection : typed(p_assign->value, p_assign->target->type, place.kind);
	} else if (p_assign->op == TokenKind::AmpAmpAssign || p_assign->op == TokenKind::PipePipeAssign) {
		// a &&= b stores b only if a is true; a ||= b only if a is false. Otherwise a stays as it is.
		value = reg(place.kind);
		emit(Op::Move, position, value, load(place, position));
		const uint32_t test = convert(value, Kind::Bool, position);
		const uint32_t skip = emit(p_assign->op == TokenKind::AmpAmpAssign ? Op::JumpIfNot : Op::JumpIf, position, test, 0);
		emit(Op::Move, position, value, typed(p_assign->value, p_assign->target->type, place.kind));
		store(place, value, position);
		patch(skip, here());
		return value;
	} else if (p_assign->op == TokenKind::PlusAssign && (place.kind == Kind::Any || untyped(p_assign->value))) {
		// `+=` with an untyped side: text or numbers, by what each holds when it runs.
		uint32_t old = settled(convert(load(place, position), Kind::Any, position), p_assign->value, position);
		uint32_t sum = reg(Kind::Any);
		emit(Op::AnyAdd, position, sum, old, any_value(p_assign->value));
		value = convert(sum, place.kind, position);
	} else {
		uint32_t old = settled(load(place, position), p_assign->value, position);
		bool strings = p_assign->op == TokenKind::PlusAssign && (place.kind == Kind::Str || is_string_type(p_assign->value->type));
		Kind right_kind = Kind::Void;
		kind_of(p_assign->value->type, &right_kind);
		bool engine = !strings && (is_engine(kind_at(old)) || is_engine(right_kind));
		uint32_t right = engine ? operand(p_assign->value, p_assign->target->type) : wide_operand(p_assign->value, strings || p_assign->op == TokenKind::SlashAssign ? Kind::Void : place.kind);
		if (engine) {
			value = convert(engine_operate(p_assign->op, { old, right }, place.kind, p_assign->target->type, position), place.kind, position);
		} else {
			value = convert(operate(p_assign->op, old, right, strings, position), place.kind, position);
		}
	}
	store(place, value, position);
	return value;
}

} // namespace ir
} // namespace kas3
