#include "query.h"

#include "ast_walk.h"

#include <algorithm>
#include <cctype>

namespace kas3 {

namespace {

const Symbol *called_symbol(const Expr *p_callee) {
	if (!p_callee) {
		return nullptr;
	}
	if (p_callee->kind == NodeKind::Name) {
		return static_cast<const Name *>(p_callee)->symbol;
	}
	if (p_callee->kind == NodeKind::Member) {
		return static_cast<const Member *>(p_callee)->resolved();
	}
	return nullptr;
}

std::optional<uint32_t> argument_at(const Queries &p_queries, const Unit &p_unit, uint32_t p_open, uint32_t p_caret) {
	const std::string &text = p_unit.source.text();
	uint32_t active = 0;
	std::vector<char> closers;
	for (uint32_t i = p_open + 1; i < p_caret; ++i) {
		if (p_queries.text_byte(p_unit, i)) {
			continue;
		}
		const char c = text[i];
		if (c == '(' || c == '[' || c == '{') {
			closers.push_back(c == '(' ? ')' : c == '[' ? ']' : '}');
		} else if (c == ')' || c == ']' || c == '}') {
			if (closers.empty() || closers.back() != c) {
				return std::nullopt;
			}
			closers.pop_back();
		} else if (c == ',' && closers.empty()) {
			++active;
		}
	}
	return active;
}

} // namespace

std::optional<SignatureHelp> Queries::signature_help(const Unit &p_unit, uint32_t p_offset) const {
	const std::string &text = p_unit.source.text();
	if (p_offset > text.size() || in_text(p_unit, p_offset)) {
		return std::nullopt;
	}
	const Expr *callee = nullptr;
	uint32_t opening = 0;
	std::optional<uint32_t> active;
	std::for_each(context_nodes(p_unit).begin(), context_nodes(p_unit).end(), [&](Node *p_node) {
		const Expr *candidate = p_node->kind == NodeKind::Call ? static_cast<Call *>(p_node)->callee :
				p_node->kind == NodeKind::New ? static_cast<New *>(p_node)->callee : nullptr;
		if (!candidate || candidate->end >= p_offset || p_node->end < p_offset) {
			return;
		}
		uint32_t open = candidate->end;
		while (open < p_offset && (std::isspace(static_cast<unsigned char>(text[open])) || text_byte(p_unit, open))) {
			++open;
		}
		if (open >= p_offset || text[open] != '(' || (callee && open <= opening)) {
			return;
		}
		if (auto argument = argument_at(*this, p_unit, open, p_offset)) {
			callee = candidate;
			opening = open;
			active = argument;
		}
	});
	const FunctionDecl *decl = function_declaration(called_symbol(callee));
	if (!decl || !decl->symbol) {
		return std::nullopt;
	}
	SignatureHelp help;
	help.label = describe(decl->symbol);
	help.documentation = documentation(decl->symbol);
	for (const Parameter &parameter : decl->parameters) {
		help.parameters.push_back(parameter_text(*decl->symbol->unit, parameter));
	}
	help.active_parameter = help.parameters.empty() ? 0 :
			std::min(*active, static_cast<uint32_t>(help.parameters.size() - 1));
	if (!help.parameters.empty()) {
		help.active_name = std::string(decl->parameters[help.active_parameter].name);
	}
	return help;
}

} // namespace kas3
