// Array, object and function literals.
#include "parser.h"

namespace kas3 {

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
