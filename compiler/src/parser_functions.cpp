// Functions: what follows `function`, in a declaration, a statement or an expression, and a function's body.
#include "parser.h"

namespace kas3 {

Block *Parser::parse_function_body() {
	Restore allow_in(no_in_, false);
	Restore brackets(bracket_ends_initializer_, false);
	return parse_block();
}

// Called just after `function`. Handles `get`/`set`, the name, parameters, return type and body.
FunctionDecl *Parser::parse_function_rest(uint32_t p_offset, Modifiers p_modifiers, List<Metadata> p_metadata,
		bool p_body_expected, bool p_allow_accessor, bool p_name_required) {
	FunctionDecl *function = node<FunctionDecl>(p_offset);
	function->modifiers = p_modifiers;
	function->metadata = p_metadata;
	// `function get name()` is an accessor; `function get()` is a function called `get`.
	if (p_allow_accessor && (at_word("get") || at_word("set")) &&
			(is_identifier_like(peek2().kind) || ((p_modifiers.bits & Modifiers::Override) &&
					(peek2().kind == TokenKind::RBrace || peek2().kind == TokenKind::EndOfFile ||
							(peek2().kind == TokenKind::LBracket && peek2().newline_before))))) {
		function->accessor = text(advance()) == "get" ? FunctionDecl::Getter : FunctionDecl::Setter;
	}
	uint32_t missing_name_end = 0;
	// A declaration on the next line belongs to the enclosing list. A name followed by '(' is
	// still a valid multiline function/accessor signature, even when that name is a modifier word.
	const bool next_declaration = p_name_required && current_.newline_before &&
			(begins_statement(current_.kind) || at_modifier_word()) && peek2().kind != TokenKind::LParen;
	if (!next_declaration && (at(TokenKind::Identifier) ||
			(function->accessor != FunctionDecl::None && is_identifier_like(current_.kind)))) {
		function->name_offset = current_.offset;
		function->name = text(advance());
	} else if (p_name_required) {
		function->name_offset = previous_end_;
		missing_name_end = current_.offset;
		error_expected("a function name");
	}

	std::vector<Parameter> parameters;
	uint32_t defaults_height = 0; // Tallest default value, which hangs below this function in the tree.
	Token open = expect(TokenKind::LParen, "'('");
	function->has_parameter_list = open.length != 0;
	if (open.length) {
		bool rest_seen = false;
		uint32_t rest_offset = 0;
		while (!at(TokenKind::RParen) && !at(TokenKind::EndOfFile)) {
			Parameter parameter;
			parameter.offset = current_.offset;
			if (rest_seen) {
				error_at(rest_offset, "a rest parameter must be last", false);
			}
			if (accept(TokenKind::Ellipsis)) {
				parameter.is_rest = true;
				rest_seen = true;
				rest_offset = parameter.offset;
			}
			if (at(TokenKind::Identifier)) {
				parameter.name = text(advance());
			} else {
				error_expected("a parameter name");
				break;
			}
			if (accept(TokenKind::Colon)) {
				parameter.type = parse_type();
			}
			if (accept(TokenKind::Assign)) {
				Restore allow_in(no_in_, false);
				Restore brackets(bracket_ends_initializer_, false);
				parameter.default_value = parse_assignment();
				if (last_height_ > defaults_height) {
					defaults_height = last_height_;
				}
			}
			parameter.end = previous_end_;
			parameters.push_back(parameter);
			if (!accept(TokenKind::Comma)) {
				break;
			}
		}
		expect_close(TokenKind::RParen, open);
	}
	function->parameters = to_list(parameters);
	if (accept(TokenKind::Colon)) {
		function->return_type = parse_type();
	}
	uint32_t height = defaults_height + 2;
	if (at(TokenKind::LBrace)) {
		// A function body starts a fresh count of statement nesting; its height is what the body reached.
		uint32_t saved_depth = statement_depth_;
		uint32_t saved_height = body_height_;
		statement_depth_ = 0;
		body_height_ = 0;
		function->body = parse_function_body();
		if (body_height_ + 3 > height) {
			height = body_height_ + 3;
		}
		statement_depth_ = saved_depth;
		body_height_ = saved_height;
	} else if (p_body_expected) {
		error_expected("'{'");
	}
	function_height_ = height;
	if (height > MAX_HEIGHT) {
		abort_too_deep();
	}
	finish(function);
	// Retain the empty name's trivia span so completion can find the unfinished declaration.
	if (function->end < missing_name_end) {
		function->end = missing_name_end;
	}
	return function;
}

} // namespace kas3
