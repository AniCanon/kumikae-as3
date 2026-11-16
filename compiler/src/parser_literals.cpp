// Literals: numbers, strings, `true`, `false`, `null`, regular expressions, arrays, objects and functions.
#include "parser.h"

namespace kas3 {

// A number, a string, `true`, `false` or `null`.
Expr *Parser::parse_literal_token() {
	switch (current_.kind) {
		case TokenKind::Number: {
			NumberLit *literal = node<NumberLit>(current_.offset);
			literal->text = text(advance());
			last_height_ = 1;
			return finish(literal);
		}
		case TokenKind::String: {
			StringLit *literal = node<StringLit>(current_.offset);
			literal->text = text(advance());
			last_height_ = 1;
			return finish(literal);
		}
		case TokenKind::KwTrue:
		case TokenKind::KwFalse: {
			BoolLit *literal = node<BoolLit>(current_.offset);
			literal->value = advance().kind == TokenKind::KwTrue;
			last_height_ = 1;
			return finish(literal);
		}
		case TokenKind::KwNull:
			last_height_ = 1;
			return finish(node<NullLit>(advance().offset));
		default:
			return missing_expression();
	}
}

Expr *Parser::parse_regex() {
	// Where an expression must begin, a slash starts a regular expression.
	current_ = lexer_.rescan_as_regex(current_);
	has_next_ = false;
	note_current();
	if (current_.kind != TokenKind::Regex) {
		advance();
		return missing_expression();
	}
	RegexLit *literal = node<RegexLit>(current_.offset);
	literal->text = text(advance());
	last_height_ = 1;
	return finish(literal);
}

Expr *Parser::parse_array_literal() {
	Restore allow_in(no_in_, false);
	Restore brackets(bracket_ends_initializer_, false);
	Token open = advance();
	ArrayLit *array = node<ArrayLit>(open.offset);
	std::vector<Expr *> elements;
	uint32_t height = 0;
	while (!at(TokenKind::RBracket) && !at(TokenKind::EndOfFile)) {
		if (at(TokenKind::Comma)) {
			advance();
			elements.push_back(nullptr); // A hole: `[1, , 2]`.
			continue;
		}
		uint32_t before = current_.offset;
		elements.push_back(parse_assignment());
		height = std::max(height, last_height_);
		if (!accept(TokenKind::Comma) || current_.offset == before) {
			break;
		}
	}
	expect_close(TokenKind::RBracket, open);
	array->elements = to_list(elements);
	grow(height + 1);
	return finish(array);
}

Expr *Parser::parse_object_literal() {
	Restore allow_in(no_in_, false);
	Restore brackets(bracket_ends_initializer_, false);
	Token open = advance();
	ObjectLit *object = node<ObjectLit>(open.offset);
	std::vector<ObjectField> fields;
	uint32_t height = 0;
	while (!at(TokenKind::RBrace) && !at(TokenKind::EndOfFile)) {
		ObjectField field;
		field.offset = current_.offset;
		if (is_identifier_like(current_.kind) || at(TokenKind::Number)) {
			field.key = text(advance());
		} else if (at(TokenKind::String)) {
			std::string_view quoted = text(advance());
			field.key = quoted.substr(1, quoted.size() - 2);
			field.key_is_string = true;
		} else {
			error_expected("a property name");
			break;
		}
		expect(TokenKind::Colon, "':'");
		field.value = parse_assignment();
		field.end = previous_end_;
		height = std::max(height, last_height_);
		fields.push_back(field);
		if (!accept(TokenKind::Comma)) {
			break;
		}
	}
	expect_close(TokenKind::RBrace, open);
	object->fields = to_list(fields);
	grow(height + 2); // The literal and its field.
	return finish(object);
}

Expr *Parser::parse_function_expression() {
	Token keyword = advance();
	FunctionExpr *expression = node<FunctionExpr>(keyword.offset);
	expression->function = parse_function_rest(keyword.offset, Modifiers(), List<Metadata>(), true, false, false);
	grow(function_height_ + 1);
	return finish(expression);
}

} // namespace kas3
