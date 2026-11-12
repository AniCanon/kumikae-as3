// Expressions and type annotations.
#include "parser.h"

#include <algorithm>

namespace kas3 {

namespace {

// Binding strength of the binary operators, weakest first. Zero means "not a binary operator".
int binary_level(TokenKind p_kind) {
	switch (p_kind) {
		case TokenKind::PipePipe:
			return 1;
		case TokenKind::AmpAmp:
			return 2;
		case TokenKind::Pipe:
			return 3;
		case TokenKind::Caret:
			return 4;
		case TokenKind::Amp:
			return 5;
		case TokenKind::Equal:
		case TokenKind::NotEqual:
		case TokenKind::StrictEqual:
		case TokenKind::StrictNotEqual:
			return 6;
		case TokenKind::Less:
		case TokenKind::Greater:
		case TokenKind::LessEqual:
		case TokenKind::GreaterEqual:
		case TokenKind::KwIs:
		case TokenKind::KwAs:
		case TokenKind::KwIn:
		case TokenKind::KwInstanceof:
			return 7;
		case TokenKind::ShiftLeft:
		case TokenKind::ShiftRight:
		case TokenKind::ShiftRightUnsigned:
			return 8;
		case TokenKind::Plus:
		case TokenKind::Minus:
			return 9;
		case TokenKind::Star:
		case TokenKind::Slash:
		case TokenKind::Percent:
			return 10;
		default:
			return 0;
	}
}

bool is_assignment(TokenKind p_kind) {
	switch (p_kind) {
		case TokenKind::Assign:
		case TokenKind::PlusAssign:
		case TokenKind::MinusAssign:
		case TokenKind::StarAssign:
		case TokenKind::SlashAssign:
		case TokenKind::PercentAssign:
		case TokenKind::ShiftLeftAssign:
		case TokenKind::ShiftRightAssign:
		case TokenKind::ShiftRightUnsignedAssign:
		case TokenKind::AmpAssign:
		case TokenKind::PipeAssign:
		case TokenKind::CaretAssign:
		case TokenKind::AmpAmpAssign:
		case TokenKind::PipePipeAssign:
			return true;
		default:
			return false;
	}
}

bool is_prefix_operator(TokenKind p_kind) {
	switch (p_kind) {
		case TokenKind::Plus:
		case TokenKind::Minus:
		case TokenKind::Bang:
		case TokenKind::Tilde:
		case TokenKind::PlusPlus:
		case TokenKind::MinusMinus:
		case TokenKind::KwTypeof:
		case TokenKind::KwVoid:
		case TokenKind::KwDelete:
			return true;
		default:
			return false;
	}
}

// Restores a parser flag when a scope ends.
struct Restore {
	bool &flag;
	bool saved;
	Restore(bool &p_flag, bool p_value) :
			flag(p_flag), saved(p_flag) {
		flag = p_value;
	}
	~Restore() { flag = saved; }
};

} // namespace

// A stand-in for an expression that could not be parsed, so that the tree stays well formed.
Expr *Parser::missing_expression() {
	Name *name = node<Name>(current_.offset);
	last_height_ = 1;
	return name;
}

Expr *Parser::parse_expression() {
	Expr *left = parse_assignment();
	uint32_t height = last_height_;
	while (at(TokenKind::Comma)) {
		advance();
		Expr *right = parse_assignment();
		height = std::max(height, last_height_) + 1;
		if (height > MAX_HEIGHT) {
			report_too_long(right->offset);
			height = MAX_HEIGHT;
			continue;
		}
		Binary *comma = node<Binary>(left->offset);
		comma->op = TokenKind::Comma;
		comma->left = left;
		comma->right = right;
		left = finish(comma);
	}
	last_height_ = height;
	return left;
}

Expr *Parser::parse_assignment() {
	if (!enter()) {
		return missing_expression();
	}
	Expr *target = parse_conditional();
	if (is_assignment(current_.kind)) {
		uint32_t target_height = last_height_;
		TokenKind op = advance().kind;
		Expr *value = parse_assignment();
		Assign *assign = node<Assign>(target->offset);
		assign->op = op;
		assign->target = target;
		assign->value = value;
		last_height_ = std::max(target_height, last_height_) + 1;
		target = finish(assign);
	}
	leave();
	return target;
}

Expr *Parser::parse_conditional() {
	Expr *condition = parse_binary(1);
	if (!at(TokenKind::Question)) {
		return condition;
	}
	uint32_t height = last_height_;
	advance();
	Expr *then_value;
	{
		Restore allow_in(no_in_, false);
		then_value = parse_assignment();
	}
	height = std::max(height, last_height_);
	expect(TokenKind::Colon, "':'");
	Expr *else_value = parse_assignment();
	height = std::max(height, last_height_);
	Conditional *conditional = node<Conditional>(condition->offset);
	conditional->condition = condition;
	conditional->then_value = then_value;
	conditional->else_value = else_value;
	last_height_ = height + 1;
	return finish(conditional);
}

// Precedence climbing. Operators of one level associate to the left, and that chain is built in this loop,
// so its length is what bounds the height of the tree.
Expr *Parser::parse_binary(int p_min_level) {
	Expr *left = parse_unary();
	uint32_t height = last_height_;
	for (;;) {
		TokenKind op = current_.kind;
		int level = binary_level(op);
		if (level == 0 || level < p_min_level || (op == TokenKind::KwIn && no_in_)) {
			break;
		}
		advance();
		Expr *right = parse_binary(level + 1);
		height = std::max(height, last_height_) + 1;
		if (height > MAX_HEIGHT) {
			report_too_long(right->offset);
			height = MAX_HEIGHT;
			continue; // Keep consuming the chain, but stop growing the tree.
		}
		Binary *binary = node<Binary>(left->offset);
		binary->op = op;
		binary->left = left;
		binary->right = right;
		left = finish(binary);
	}
	last_height_ = height;
	return left;
}

Expr *Parser::parse_unary() {
	if (!is_prefix_operator(current_.kind)) {
		return parse_postfix();
	}
	if (!enter()) {
		return missing_expression();
	}
	Token op = advance();
	Unary *unary = node<Unary>(op.offset);
	unary->op = op.kind;
	unary->operand = parse_unary();
	last_height_++;
	leave();
	return finish(unary);
}

Expr *Parser::parse_postfix() {
	Expr *operand = parse_call_member(true);
	// `++` and `--` on a new line belong to the next statement.
	if ((at(TokenKind::PlusPlus) || at(TokenKind::MinusMinus)) && !current_.newline_before) {
		Postfix *postfix = node<Postfix>(operand->offset);
		postfix->op = advance().kind;
		postfix->operand = operand;
		last_height_++;
		return finish(postfix);
	}
	return operand;
}

std::string_view Parser::parse_property_name(const char *p_what) {
	// Reserved words are ordinary names after a dot: event.type, obj.default, x.class.
	if (is_identifier_like(current_.kind)) {
		return text(advance());
	}
	error_expected(p_what);
	return std::string_view();
}

List<Expr *> Parser::parse_arguments(uint32_t *r_height) {
	Restore allow_in(no_in_, false);
	Restore brackets(bracket_ends_initializer_, false);
	std::vector<Expr *> arguments;
	uint32_t height = 0;
	advance(); // '('
	while (!at(TokenKind::RParen) && !at(TokenKind::EndOfFile)) {
		uint32_t before = current_.offset;
		arguments.push_back(parse_assignment());
		height = std::max(height, last_height_);
		if (!accept(TokenKind::Comma)) {
			break;
		}
		if (current_.offset == before) {
			break;
		}
	}
	expect(TokenKind::RParen, "')'");
	*r_height = height;
	return to_list(arguments);
}

// A primary expression followed by any number of `.name`, `[index]`, `.<Type>` and, if allowed, `(args)`.
Expr *Parser::parse_call_member(bool p_allow_calls) {
	Expr *left = parse_primary();
	uint32_t height = last_height_;
	bool overflow = false;
	for (;;) {
		Expr *link = nullptr;
		uint32_t child_height = 0;
		if (at(TokenKind::Dot) || at(TokenKind::DotDot) || at(TokenKind::ColonColon)) {
			if (at(TokenKind::DotDot)) {
				error_at(current_.offset, "E4X (XML syntax) is not supported", false);
			} else if (at(TokenKind::ColonColon)) {
				error_at(current_.offset, "namespaces are not supported", false);
			}
			advance();
			if (at(TokenKind::At)) {
				error_at(current_.offset, "E4X (XML syntax) is not supported", false);
				advance();
			}
			Member *member = node<Member>(left->offset);
			member->object = left;
			member->name = parse_property_name("a property name");
			link = member;
		} else if (at(TokenKind::DotLess)) {
			advance();
			TypeApply *apply = node<TypeApply>(left->offset);
			apply->base = left;
			apply->argument = parse_type();
			expect_type_close();
			link = apply;
		} else if (at(TokenKind::LBracket)) {
			// In a member or top-level initialiser, a `[` that starts a line is the next member's metadata.
			if (bracket_ends_initializer_ && current_.newline_before) {
				break;
			}
			advance();
			Index *index = node<Index>(left->offset);
			index->object = left;
			{
				Restore allow_in(no_in_, false);
				Restore brackets(bracket_ends_initializer_, false);
				index->index = parse_expression();
			}
			child_height = last_height_;
			expect(TokenKind::RBracket, "']'");
			link = index;
		} else if (at(TokenKind::LParen) && p_allow_calls) {
			Call *call = node<Call>(left->offset);
			call->callee = left;
			call->arguments = parse_arguments(&child_height);
			link = call;
		} else {
			break;
		}
		height = std::max(height, child_height) + 1;
		if (height > MAX_HEIGHT) {
			report_too_long(current_.offset);
			height = MAX_HEIGHT;
			overflow = true;
		}
		if (!overflow) {
			left = finish(link);
		}
	}
	last_height_ = height;
	return left;
}

Expr *Parser::parse_new() {
	Token keyword = advance();
	// `new <int>[1, 2]` is a vector literal.
	if (at(TokenKind::Less)) {
		advance();
		VectorLit *vector = node<VectorLit>(keyword.offset);
		vector->element_type = parse_type();
		expect_type_close();
		uint32_t height = 0;
		std::vector<Expr *> elements;
		if (expect(TokenKind::LBracket, "'['").length) {
			Restore allow_in(no_in_, false);
			Restore brackets(bracket_ends_initializer_, false);
			while (!at(TokenKind::RBracket) && !at(TokenKind::EndOfFile)) {
				uint32_t before = current_.offset;
				elements.push_back(parse_assignment());
				height = std::max(height, last_height_);
				if (!accept(TokenKind::Comma) || current_.offset == before) {
					break;
				}
			}
			expect(TokenKind::RBracket, "']'");
		}
		vector->elements = to_list(elements);
		last_height_ = height + 1;
		return finish(vector);
	}
	New *create = node<New>(keyword.offset);
	create->callee = parse_call_member(false);
	uint32_t height = last_height_;
	if (at(TokenKind::LParen)) {
		uint32_t arguments_height = 0;
		create->arguments = parse_arguments(&arguments_height);
		create->has_arguments = true;
		height = std::max(height, arguments_height);
	}
	last_height_ = height + 1;
	return finish(create);
}

Expr *Parser::parse_array_literal() {
	Restore allow_in(no_in_, false);
	Restore brackets(bracket_ends_initializer_, false);
	ArrayLit *array = node<ArrayLit>(advance().offset);
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
	expect(TokenKind::RBracket, "']'");
	array->elements = to_list(elements);
	last_height_ = height + 1;
	return finish(array);
}

Expr *Parser::parse_object_literal() {
	Restore allow_in(no_in_, false);
	Restore brackets(bracket_ends_initializer_, false);
	ObjectLit *object = node<ObjectLit>(advance().offset);
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
		height = std::max(height, last_height_);
		fields.push_back(field);
		if (!accept(TokenKind::Comma)) {
			break;
		}
	}
	expect(TokenKind::RBrace, "'}'");
	object->fields = to_list(fields);
	last_height_ = height + 1;
	return finish(object);
}

Expr *Parser::parse_function_expression() {
	Token keyword = advance();
	FunctionExpr *expression = node<FunctionExpr>(keyword.offset);
	expression->function = parse_function_rest(keyword.offset, Modifiers(), List<Metadata>(), true, false, false);
	last_height_ = 1;
	return finish(expression);
}

Expr *Parser::parse_primary() {
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
		case TokenKind::Slash:
		case TokenKind::SlashAssign: {
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
		case TokenKind::KwThis:
			last_height_ = 1;
			return finish(node<ThisExpr>(advance().offset));
		case TokenKind::KwSuper:
			last_height_ = 1;
			return finish(node<SuperExpr>(advance().offset));
		case TokenKind::Identifier: {
			Name *name = node<Name>(current_.offset);
			name->text = text(advance());
			last_height_ = 1;
			return finish(name);
		}
		case TokenKind::LParen: {
			advance();
			Expr *inner;
			{
				Restore allow_in(no_in_, false);
				Restore brackets(bracket_ends_initializer_, false);
				inner = parse_expression();
			}
			expect(TokenKind::RParen, "')'");
			return inner;
		}
		case TokenKind::LBracket:
			return parse_array_literal();
		case TokenKind::LBrace:
			return parse_object_literal();
		case TokenKind::KwFunction:
			return parse_function_expression();
		case TokenKind::KwNew:
			return parse_new();
		case TokenKind::At:
			error_at(current_.offset, "E4X (XML syntax) is not supported", false);
			advance();
			return parse_primary();
		case TokenKind::Less:
			// Inline XML. Skip to the end of the statement or line, whichever comes first.
			error_at(current_.offset, "E4X (XML syntax) is not supported", false);
			skip_xml();
			return missing_expression();
		case TokenKind::Error:
			// The lexer reported it. Take it as the expression and carry on.
			advance();
			return missing_expression();
		default:
			error_expected("an expression");
			return missing_expression();
	}
}

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
	if (!enter()) {
		return type;
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
	} else if (at(TokenKind::KwVoid)) {
		advance();
		type->form = TypeRef::Void;
	} else if (at(TokenKind::Identifier)) {
		std::vector<std::string_view> parts;
		parts.push_back(text(advance()));
		while (at(TokenKind::Dot) && peek2().kind == TokenKind::Identifier) {
			advance();
			parts.push_back(text(advance()));
		}
		type->form = TypeRef::Named;
		type->name = to_list(parts);
		if (at(TokenKind::DotLess)) {
			advance();
			type->form = TypeRef::Applied;
			type->argument = parse_type();
			expect_type_close();
		}
	} else {
		error_expected("a type");
	}
	leave();
	return finish(type);
}

// `a.b.C`, and for imports `a.b.*`.
List<std::string_view> Parser::parse_dotted_name(const char *p_what, bool *r_wildcard) {
	std::vector<std::string_view> parts;
	if (r_wildcard) {
		*r_wildcard = false;
	}
	if (!at(TokenKind::Identifier)) {
		error_expected(p_what);
		return to_list(parts);
	}
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
		parts.push_back(text(advance()));
	}
	return to_list(parts);
}

} // namespace kas3
