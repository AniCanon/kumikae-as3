#include "typer.h"

namespace kas3 {

const Type * Typer::tag_type(Symbol *p_class, const Metadata &p_tag, std::string_view p_name) {
	if (p_name == "*") {
		return any_;
	}
	Package *package = p_class->package;
	std::string_view name = p_name;
	size_t dot = p_name.rfind('.');
	if (dot != std::string_view::npos) {
		package = compilation_.find_package(std::string(p_name.substr(0, dot)));
		name = p_name.substr(dot + 1);
	}
	for (Package *place : { package, dot == std::string_view::npos ? compilation_.top_level() : nullptr }) {
		if (!place) {
			continue;
		}
		auto found = place->definitions.find(name);
		if (found != place->definitions.end() &&
				(found->second->kind == SymbolKind::Class || found->second->kind == SymbolKind::Interface)) {
			return compilation_.instance_type(found->second);
		}
	}
	p_class->unit->diagnostics.error(p_tag.offset, "unknown type " + quoted(p_name) + " in an [" +
														   std::string(p_tag.name) + "] tag");
	return nullptr;
}

auto Typer::described(const Type *p_type) -> const Described * {
	Symbol *symbol = p_type->kind == Type::Instance ? p_type->symbol : nullptr;
	if (!symbol || symbol->kind != SymbolKind::Class || !symbol->unit || !symbol->unit->is_declaration ||
			!symbol->node || symbol->node->kind != NodeKind::ClassDecl) {
		return nullptr;
	}
	auto cached = described_.find(symbol);
	if (cached != described_.end()) {
		return cached->second.complete || cached->second.element ? &cached->second : nullptr;
	}
	Described result;
	// A subclass is one of its base, so it has what its base has.
	if (symbol->klass && symbol->klass->base) {
		if (const Described *base = described(compilation_.instance_type(symbol->klass->base))) {
			result = *base;
		}
	}
	for (const Metadata &tag : static_cast<ClassDecl *>(symbol->node)->metadata) {
		std::string_view op, left, right, returns, element, keyed;
		for (const MetadataArg &argument : tag.args) {
			if (argument.key == "op") {
				op = argument.value;
			} else if (argument.key == "left") {
				left = argument.value;
			} else if (argument.key == "right") {
				right = argument.value;
			} else if (argument.key == "returns") {
				returns = argument.value;
			} else if (argument.key == "element") {
				element = argument.value;
			} else if (argument.key == "keyed") {
				keyed = argument.value;
			}
		}
		if (tag.name == "Indexed") {
			if (const Type *type = tag_type(symbol, tag, element)) {
				result.element = type;
				result.keyed = keyed == "true";
			}
		} else if (tag.name == "Operators") {
			result.complete = true;
		} else if (tag.name == "Operator") {
			result.complete = true;
			OperatorRule rule;
			rule.op = op;
			rule.result = tag_type(symbol, tag, returns);
			if (!left.empty()) {
				rule.side = OperatorRule::Left;
				rule.other = tag_type(symbol, tag, left);
			} else if (!right.empty()) {
				rule.other = tag_type(symbol, tag, right);
			} else {
				rule.side = OperatorRule::Unary;
			}
			// A tag that names a type the program does not have describes nothing.
			if (!op.empty() && rule.result && (rule.other || rule.side == OperatorRule::Unary)) {
				result.operators.push_back(rule);
			}
		}
	}
	const Described &stored = described_.emplace(symbol, std::move(result)).first->second;
	return stored.complete || stored.element ? &stored : nullptr;
}

bool Typer::has_operators(const Type *p_type) {
	const Described *found = described(p_type);
	return found && found->complete;
}

bool Typer::whole(const Expr *p_expression) const {
	if (!p_expression || !p_expression->type) {
		return false;
	}
	if (p_expression->kind == NodeKind::Binary) {
		const Binary *binary = static_cast<const Binary *>(p_expression);
		switch (binary->op) {
			case TokenKind::Plus:
			case TokenKind::Minus:
			case TokenKind::Star:
			case TokenKind::Percent:
				return whole(binary->left) && whole(binary->right);
			default:
				break;
		}
	}
	if (p_expression->kind == NodeKind::Unary) {
		const Unary *unary = static_cast<const Unary *>(p_expression);
		if (unary->op == TokenKind::Minus || unary->op == TokenKind::Plus) {
			return whole(unary->operand);
		}
	}
	if (p_expression->kind == NodeKind::Conditional) {
		const Conditional *choice = static_cast<const Conditional *>(p_expression);
		return whole(choice->then_value) && whole(choice->else_value);
	}
	if (p_expression->kind == NodeKind::Call) {
		// What AS3's own functions give that is whole, though they hand it back in a Number.
		const Call *call = static_cast<const Call *>(p_expression);
		const Symbol *symbol = call->callee && call->callee->kind == NodeKind::Member ? (static_cast<const Member *>(call->callee)->symbol ? static_cast<const Member *>(call->callee)->symbol : static_cast<const Member *>(call->callee)->member)
				: call->callee && call->callee->kind == NodeKind::Name ? static_cast<const Name *>(call->callee)->symbol
																		: nullptr;
		if (symbol && symbol->unit && symbol->unit->is_builtin) {
			bool math = symbol->owner && symbol->owner->name == "Math" && symbol->kind == SymbolKind::Method;
			if ((math && (symbol->name == "floor" || symbol->name == "round" || symbol->name == "ceil")) ||
					(!symbol->owner && symbol->kind == SymbolKind::Function && symbol->name == "parseInt")) {
				return true;
			}
			if (math && (symbol->name == "abs" || symbol->name == "max" || symbol->name == "min") && call->arguments.count > 0) {
				for (const Expr *argument : call->arguments) {
					if (!whole(argument)) {
						return false;
					}
				}
				return true;
			}
		}
	}
	return integral(p_expression->type);
}

bool Typer::operand_matches(const Expr *p_operand, const Type *p_type, const Type *p_wanted, bool *r_exact) const {
	*r_exact = true;
	if (numeric(p_wanted)) {
		if (!numeric(p_type)) {
			return false;
		}
		// A whole number is also a Number, so it may use a form declared for Number when there is no
		// other. A fraction is never a whole number.
		bool is_whole = whole(p_operand);
		*r_exact = is_whole == (p_wanted != number_);
		return *r_exact || is_whole;
	}
	if (p_wanted == boolean_ || p_wanted == string_ || numeric(p_type) || p_type == boolean_ || p_type == string_) {
		return p_type == p_wanted; // Everything converts to these two, and they to nothing here.
	}
	return fits(p_type, p_wanted) == Fit::Yes;
}

bool Typer::open_ended(const Type *p_type) const {
	return p_type->kind == Type::Any || p_type->kind == Type::Unknown || p_type->kind == Type::Null ||
			p_type == object_;
}

const Type * Typer::described_binary(TokenKind p_op, Expr *p_left, Expr *p_right, uint32_t p_offset) {
	const Type *left = p_left ? p_left->type : nullptr;
	const Type *right = p_right ? p_right->type : nullptr;
	if (!left || !right || (!has_operators(left) && !has_operators(right))) {
		return nullptr;
	}
	if (open_ended(left) || open_ended(right)) {
		return unknown_;
	}
	std::string_view op = token_kind_spelling(p_op);
	const Type *loose = nullptr;
	for (int pass = 0; pass < 2; pass++) {
		const Described *rules = described(pass == 0 ? left : right);
		if (!rules) {
			continue;
		}
		for (const OperatorRule &rule : rules->operators) {
			if (rule.op != op || rule.side != (pass == 0 ? OperatorRule::Right : OperatorRule::Left)) {
				continue;
			}
			bool exact = true;
			if (!operand_matches(pass == 0 ? p_right : p_left, pass == 0 ? right : left, rule.other, &exact)) {
				continue;
			}
			if (exact) {
				return rule.result;
			}
			if (!loose) {
				loose = rule.result;
			}
		}
	}
	if (loose) {
		return loose;
	}
	error(p_offset, "can't use " + quoted(op) + " on " + quoted(compilation_.type_name(left)) + " and " +
							quoted(compilation_.type_name(right)));
	return unknown_;
}

const Type * Typer::described_unary(TokenKind p_op, const Type *p_operand, uint32_t p_offset) {
	const Described *rules = has_operators(p_operand) ? described(p_operand) : nullptr;
	if (!rules) {
		return nullptr;
	}
	std::string_view op = token_kind_spelling(p_op);
	for (const OperatorRule &rule : rules->operators) {
		if (rule.side == OperatorRule::Unary && rule.op == op) {
			return rule.result;
		}
	}
	error(p_offset, "can't use " + quoted(op) + " on " + quoted(compilation_.type_name(p_operand)));
	return unknown_;
}

bool Typer::never_operator(TokenKind p_op, const Type *p_operand, uint32_t p_offset) {
	if (!has_operators(p_operand)) {
		return false;
	}
	error(p_offset, "can't use " + quoted(token_kind_spelling(p_op)) + " on " + quoted(compilation_.type_name(p_operand)));
	return true;
}

TokenKind Typer::compound_operator(TokenKind p_op) {
	switch (p_op) {
		case TokenKind::PlusAssign:
			return TokenKind::Plus;
		case TokenKind::MinusAssign:
			return TokenKind::Minus;
		case TokenKind::StarAssign:
			return TokenKind::Star;
		case TokenKind::SlashAssign:
			return TokenKind::Slash;
		case TokenKind::PercentAssign:
			return TokenKind::Percent;
		default:
			return TokenKind::Error;
	}
}

} // namespace kas3
