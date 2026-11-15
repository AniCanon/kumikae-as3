#include "query.h"

#include <algorithm>
#include <cctype>

namespace kas3 {

const FunctionDecl *Queries::function_declaration(const Symbol *p_symbol) {
	if (p_symbol && p_symbol->klass) {
		p_symbol = p_symbol->klass->constructor;
	}
	return p_symbol && p_symbol->node && p_symbol->node->kind == NodeKind::FunctionDecl ?
			static_cast<const FunctionDecl *>(p_symbol->node) : nullptr;
}

const Completion &Queries::completion_item(Symbol *p_symbol) {
	auto [entry, inserted] = completion_items_.try_emplace(p_symbol);
	if (inserted) {
		Completion &item = entry->second;
		item.name = std::string(p_symbol->name);
		item.kind = p_symbol->kind;
		item.is_const = p_symbol->is_const;
		item.detail = describe(p_symbol);
		const FunctionDecl *decl = function_declaration(p_symbol);
		item.callable = decl && !p_symbol->is_type() && decl->accessor == FunctionDecl::None;
		if (item.callable) {
			for (const Parameter &parameter : decl->parameters) {
				if (!parameter.default_value && !parameter.is_rest) {
					item.required_parameters.emplace_back(parameter.name);
				}
			}
		}
	}
	return entry->second;
}

CompletionRange Queries::completion_range(const Unit &p_unit, uint32_t p_offset) const {
	const std::string &text = p_unit.source.text();
	const uint32_t caret = std::min(p_offset, static_cast<uint32_t>(text.size()));
	CompletionRange result{ caret, caret, caret, false };
	while (result.begin && is_identifier_at(text, result.begin - 1)) {
		--result.begin;
	}
	while (result.end < text.size() && is_identifier_at(text, result.end)) {
		++result.end;
	}
	uint32_t after = result.end;
	while (after < text.size() && std::isspace(static_cast<unsigned char>(text[after]))) {
		++after;
	}
	if (after < text.size() && text[after] == '(') {
		return result;
	}
	std::for_each(context_nodes(p_unit).begin(), context_nodes(p_unit).end(), [&](Node *p_node) {
		if (p_node->kind != NodeKind::ExprStmt || p_node->offset > result.begin || p_node->end < caret) {
			return;
		}
		const Expr *expr = static_cast<ExprStmt *>(p_node)->expr;
		result.call = expr && (expr->kind == NodeKind::Name || expr->kind == NodeKind::Member);
	});
	return result;
}

} // namespace kas3
