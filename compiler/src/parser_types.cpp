// Type annotations, and names written with dots.
#include "parser.h"

namespace kas3 {

// One `>` closing a type argument list. The lexer applies longest match, so `Vector.<Vector.<int>>` ends in
// a single `>>` token: take one `>` from it and leave the rest as the current token.
void Parser::expect_type_close() {
	TokenKind rest;
	switch (current_.kind) {
		case TokenKind::Greater:
			advance();
			return;
		case TokenKind::ShiftRight:
			rest = TokenKind::Greater;
			break;
		case TokenKind::ShiftRightUnsigned:
			rest = TokenKind::ShiftRight;
			break;
		case TokenKind::GreaterEqual:
			rest = TokenKind::Assign;
			break;
		case TokenKind::ShiftRightAssign:
			rest = TokenKind::GreaterEqual;
			break;
		case TokenKind::ShiftRightUnsignedAssign:
			rest = TokenKind::ShiftRightAssign;
			break;
		default:
			error_expected("'>'");
			return;
	}
	previous_end_ = current_.offset + 1;
	current_.kind = rest;
	current_.offset += 1;
	current_.length -= 1;
	current_.newline_before = false;
}

TypeRef *Parser::parse_type() {
	TypeRef *type = node<TypeRef>(current_.offset);
	type->completion_begin = previous_end_;
	if (!enter()) {
		return type;
	}
	// Type arguments nest by recursion and are not part of any expression's height, so they get their own,
	// much smaller cap.
	if (++type_depth_ > MAX_TYPE_DEPTH) {
		abort_too_deep();
	}
	if (at(TokenKind::Star)) {
		advance();
	} else if (at(TokenKind::StarAssign)) {
		// `var x:*=1` lexes `*=` as one token: the type is `*` and an `=` remains.
		previous_end_ = current_.offset + 1;
		current_.kind = TokenKind::Assign;
		current_.offset += 1;
		current_.length -= 1;
		current_.newline_before = false;
	} else if (at(TokenKind::LParen)) {
		Token open = advance();
		type->form = TypeRef::Callback;
		std::vector<TypeRef *> parameters;
		if (!at(TokenKind::RParen)) {
			do {
				parameters.push_back(parse_type());
			} while (accept(TokenKind::Comma));
		}
		expect_close(TokenKind::RParen, open);
		expect(TokenKind::Assign, "'=>'");
		expect(TokenKind::Greater, "'=>'");
		type->parameters = to_list(parameters);
		type->argument = parse_type();
	} else if (at(TokenKind::KwVoid)) {
		advance();
		type->form = TypeRef::Void;
	} else if (at(TokenKind::Identifier)) {
		std::vector<std::string_view> parts;
		std::vector<uint32_t> offsets;
		offsets.push_back(current_.offset);
		parts.push_back(text(advance()));
		while (at(TokenKind::Dot) && peek2().kind == TokenKind::Identifier) {
			advance();
			offsets.push_back(current_.offset);
			parts.push_back(text(advance()));
		}
		type->form = TypeRef::Named;
		type->name = to_list(parts);
		type->name_offsets = to_list(offsets);
		if (at(TokenKind::DotLess)) {
			advance();
			type->form = TypeRef::Applied;
			type->argument = parse_type();
			expect_type_close();
		}
	} else {
		error_expected("a type");
		// Nothing is written. The type's place runs from what came before it to what follows, so that an
		// editor can tell that a type goes there.
		type->offset = previous_end_;
		type->end = current_.offset;
		type_depth_--;
		leave();
		return type;
	}
	type_depth_--;
	leave();
	return finish(type);
}

// `a.b.C`, and for imports `a.b.*`.
List<std::string_view> Parser::parse_dotted_name(const char *p_what, bool *r_wildcard, List<uint32_t> *r_offsets) {
	std::vector<std::string_view> parts;
	std::vector<uint32_t> offsets;
	if (r_wildcard) {
		*r_wildcard = false;
	}
	if (!at(TokenKind::Identifier)) {
		error_expected(p_what);
		return to_list(parts);
	}
	offsets.push_back(current_.offset);
	parts.push_back(text(advance()));
	while (at(TokenKind::Dot)) {
		advance();
		if (r_wildcard && at(TokenKind::Star)) {
			advance();
			*r_wildcard = true;
			break;
		}
		if (!is_identifier_like(current_.kind)) {
			error_expected(p_what);
			break;
		}
		offsets.push_back(current_.offset);
		parts.push_back(text(advance()));
	}
	if (r_offsets) {
		*r_offsets = to_list(offsets);
	}
	return to_list(parts);
}

} // namespace kas3
