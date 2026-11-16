// Constants written in the source: number and string literals, and the built-in library's constants
// (NaN, Infinity, the limits of the number types).
#include "lowerer.h"

namespace kas3 {
namespace ir {

std::optional<int64_t> Lowerer::wide_literal(const Expr *p_expression) {
	const WideLiteral literal = read_wide_literal(p_expression);
	return literal.state == WideLiteral::State::Fits ? std::optional<int64_t>(literal.value) : std::nullopt;
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

} // namespace ir
} // namespace kas3
