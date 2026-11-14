#include "typer.h"

namespace kas3 {

bool Typer::wide_whole(const Expr *p_expression) {
	bool negative = false;
	while (p_expression && p_expression->kind == NodeKind::Unary &&
			(static_cast<const Unary *>(p_expression)->op == TokenKind::Minus || static_cast<const Unary *>(p_expression)->op == TokenKind::Plus)) {
		negative = negative != (static_cast<const Unary *>(p_expression)->op == TokenKind::Minus);
		p_expression = static_cast<const Unary *>(p_expression)->operand;
	}
	if (!p_expression || p_expression->kind != NodeKind::NumberLit) {
		return false;
	}
	uint64_t value = 0;
	bool whole_number = false;
	return wide_literal(static_cast<const NumberLit *>(p_expression)->text, &value, &whole_number) &&
			value <= (negative ? 9223372036854775808ull : 9223372036854775807ull);
}

bool Typer::wide_literal(std::string_view p_text, uint64_t *r_value, bool *r_whole) {
	bool hex = p_text.size() > 2 && p_text[0] == '0' && (p_text[1] == 'x' || p_text[1] == 'X');
	*r_whole = hex || p_text.find_first_of(".eE") == std::string_view::npos;
	if (!*r_whole) {
		return false;
	}
	uint64_t value = 0;
	for (size_t i = hex ? 2 : 0; i < p_text.size(); i++) {
		char c = p_text[i];
		int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
		uint64_t base = hex ? 16 : 10;
		if (digit >= (int)base || value > (UINT64_MAX - (uint64_t)digit) / base) {
			return false;
		}
		value = value * base + (uint64_t)digit;
	}
	*r_value = value;
	return true;
}

const Type *Typer::number_literal(const NumberLit *p_literal) const {
	unsigned long long value = 0;
	if (!whole_value(p_literal, &value)) {
		return number_;
	}
	if (value <= 2147483647ull) {
		return int_;
	}
	return value <= 4294967295ull ? uint_ : number_;
}

bool Typer::whole_value(const NumberLit *p_literal, unsigned long long *r_value) const {
	std::string text(p_literal->text);
	bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
	if (!hex && text.find_first_of(".eE") != std::string::npos) {
		return false;
	}
	size_t start = hex ? 2 : 0;
	while (start + 1 < text.size() && text[start] == '0') {
		start++;
	}
	std::string digits = text.substr(start);
	if (digits.empty() || digits.size() > 15) {
		return false;
	}
	char *end = nullptr;
	*r_value = std::strtoull(digits.c_str(), &end, hex ? 16 : 10);
	return end && *end == 0;
}

} // namespace kas3
