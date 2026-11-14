#include "whole_numbers.h"

namespace kas3 {

WideLiteral read_wide_literal(const Expr *p_expression) {
	WideLiteral result;
	while (p_expression && p_expression->kind == NodeKind::Unary &&
			(static_cast<const Unary *>(p_expression)->op == TokenKind::Minus || static_cast<const Unary *>(p_expression)->op == TokenKind::Plus)) {
		result.negative = result.negative != (static_cast<const Unary *>(p_expression)->op == TokenKind::Minus);
		p_expression = static_cast<const Unary *>(p_expression)->operand;
	}
	if (!p_expression || p_expression->kind != NodeKind::NumberLit) {
		return result;
	}
	const std::string_view text = static_cast<const NumberLit *>(p_expression)->text;
	const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
	if (!hex && text.find_first_of(".eE") != std::string_view::npos) {
		return result;
	}
	result.digits = text;
	result.state = WideLiteral::State::TooLarge;
	const uint64_t base = hex ? 16 : 10;
	uint64_t value = 0;
	for (size_t i = hex ? 2 : 0; i < text.size(); i++) {
		const char c = text[i];
		const uint64_t digit = c >= '0' && c <= '9' ? (uint64_t)(c - '0')
				: c >= 'a' && c <= 'f' ? (uint64_t)(c - 'a' + 10)
				: c >= 'A' && c <= 'F' ? (uint64_t)(c - 'A' + 10)
									   : base;
		if (digit >= base || value > (UINT64_MAX - digit) / base) {
			return result;
		}
		value = value * base + digit;
	}
	if (value > (result.negative ? (uint64_t)1 << 63 : (uint64_t)INT64_MAX)) {
		return result;
	}
	result.state = WideLiteral::State::Fits;
	result.value = result.negative ? (int64_t)(0 - value) : (int64_t)value;
	return result;
}

WholeResult gives_whole_number(const Call *p_call) {
	const Expr *callee = p_call->callee;
	const Symbol *symbol = !callee ? nullptr
			: callee->kind == NodeKind::Member ? static_cast<const Member *>(callee)->resolved()
			: callee->kind == NodeKind::Name ? static_cast<const Name *>(callee)->symbol
											 : nullptr;
	if (!symbol || !symbol->unit || !symbol->unit->is_builtin) {
		return WholeResult::No;
	}
	if (!symbol->owner) {
		return symbol->kind == SymbolKind::Function && symbol->name == "parseInt" ? WholeResult::Always : WholeResult::No;
	}
	if (symbol->owner->name != "Math" || symbol->kind != SymbolKind::Method) {
		return WholeResult::No;
	}
	if (symbol->name == "floor" || symbol->name == "round" || symbol->name == "ceil") {
		return WholeResult::Always;
	}
	const bool by_arguments = (symbol->name == "abs" || symbol->name == "max" || symbol->name == "min") && p_call->arguments.count > 0;
	return by_arguments ? WholeResult::IfArgumentsAre : WholeResult::No;
}

bool has_tag(const Symbol *p_class, std::string_view p_tag) {
	if (!p_class || p_class->kind != SymbolKind::Class || !p_class->unit || !p_class->unit->is_declaration || !p_class->node ||
			p_class->node->kind != NodeKind::ClassDecl) {
		return false;
	}
	for (const Metadata &tag : static_cast<const ClassDecl *>(p_class->node)->metadata) {
		if (tag.name == p_tag) {
			return true;
		}
	}
	return false;
}

} // namespace kas3
