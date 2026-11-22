// Expressions. Literals are in parser_literals.cpp, and type annotations in parser_types.cpp.
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
		uint32_t taller = std::max(height, last_height_) + 1;
		if (taller > MAX_CHAIN) {
			report_too_long(right->offset);
			continue; // Keep consuming, but stop growing the tree.
		}
		height = taller;
		Binary *comma = node<Binary>(left->offset);
		comma->op = TokenKind::Comma;
		comma->left = left;
		comma->right = right;
		left = finish(comma);
	}
	last_height_ = height;
	note_height();
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
		grow(std::max(target_height, last_height_) + 1);
		target = finish(assign);
	}
	note_height();
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
	grow(height + 1);
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
		Expr *right;
		if ((op == TokenKind::KwIs || op == TokenKind::KwAs) && at(TokenKind::LBracket) && peek2().kind == TokenKind::RBracket) {
			TypeApply *apply = node<TypeApply>(current_.offset);
			apply->argument = parse_type();
			right = finish(apply);
			last_height_ = 1;
		} else {
			right = parse_binary(level + 1);
		}
		uint32_t taller = std::max(height, last_height_) + 1;
		if (taller > MAX_CHAIN) {
			report_too_long(right->offset);
			continue; // Keep consuming the chain, but stop growing the tree.
		}
		height = taller;
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
	grow(last_height_ + 1);
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
		grow(last_height_ + 1);
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
	Token open = advance(); // '('
	while (!at(TokenKind::RParen) && !at(TokenKind::EndOfFile)) {
		uint32_t before = current_.offset;
		arguments.push_back(parse_assignment());
		height = std::max(height, last_height_);
		if (!accept(TokenKind::Comma)) {
			break;
		}
		if (at(TokenKind::RParen)) {
			// `f(a,)`: AS3 has no trailing comma in arguments.
			error_at(current_.offset, "expected an expression but found ')'", false);
			break;
		}
		if (current_.offset == before) {
			break;
		}
	}
	expect_close(TokenKind::RParen, open);
	*r_height = height;
	return to_list(arguments);
}

// After the object: `.name`, and the E4X and namespace forms of it, which are reported.
Member *Parser::parse_member_link(Expr *p_object) {
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
	Member *member = node<Member>(p_object->offset);
	member->object = p_object;
	member->name_offset = current_.offset;
	member->name = parse_property_name("a property name");
	return member;
}

// A primary expression followed by any number of `.name`, `[index]`, `.<Type>` and, if allowed, `(args)`.
Expr *Parser::parse_call_member(bool p_allow_calls) {
	return parse_member_tail(parse_primary(), p_allow_calls);
}

Expr *Parser::parse_member_tail(Expr *left, bool p_allow_calls) {
	uint32_t height = last_height_;
	for (;;) {
		Expr *link = nullptr;
		uint32_t child_height = 0;
		if (at(TokenKind::Dot) || at(TokenKind::DotDot) || at(TokenKind::ColonColon)) {
			link = parse_member_link(left);
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
			Token open = advance();
			Index *index = node<Index>(left->offset);
			index->object = left;
			{
				Restore allow_in(no_in_, false);
				Restore brackets(bracket_ends_initializer_, false);
				index->index = parse_expression();
			}
			child_height = last_height_;
			expect_close(TokenKind::RBracket, open);
			link = index;
		} else if (at(TokenKind::LParen) && p_allow_calls) {
			Call *call = node<Call>(left->offset);
			call->callee = left;
			call->arguments = parse_arguments(&child_height);
			link = call;
		} else {
			break;
		}
		uint32_t taller = std::max(height, child_height) + 1;
		if (taller > MAX_CHAIN) {
			report_too_long(current_.offset);
			continue; // Keep consuming the chain, but stop growing the tree.
		}
		height = taller;
		left = finish(link);
	}
	last_height_ = height;
	return left;
}

Expr *Parser::parse_new() {
	if (!enter()) {
		return missing_expression();
	}
	Token keyword = advance();
	ArrayLit *size = at(TokenKind::LBracket) ? static_cast<ArrayLit *>(parse_array_literal()) : nullptr;
	const bool fixed = size && !current_.newline_before && (at(TokenKind::Identifier) || at(TokenKind::Star) || at(TokenKind::KwVoid) ||
			(at(TokenKind::LBracket) && peek2().kind == TokenKind::RBracket));
	if (fixed || (!size && accept(TokenKind::Less))) {
		VectorLit *vector = node<VectorLit>(keyword.offset);
		uint32_t height = fixed ? last_height_ : 0;
		vector->fixed_array = fixed;
		if (fixed) {
			TypeRef *array_type = node<TypeRef>(size->offset);
			array_type->form = TypeRef::FixedArray;
			array_type->argument = parse_type();
			vector->element_type = finish(array_type);
		} else {
			vector->element_type = parse_type();
		}
		if (fixed && size->elements.count) {
			if (size->elements.count != 1 || !size->elements[0]) {
				error_at(size->offset, "a fixed array requires one length");
			}
			vector->length = size->elements[0];
			grow(height + 1);
			leave();
			return finish(vector);
		}
		if (!fixed) {
			expect_type_close();
		}
		TokenKind start = fixed ? TokenKind::LBrace : TokenKind::LBracket;
		TokenKind end = fixed ? TokenKind::RBrace : TokenKind::RBracket;
		std::vector<Expr *> elements;
		if (at(start)) {
			Token open = advance();
			Restore allow_in(no_in_, false);
			Restore brackets(bracket_ends_initializer_, false);
			while (!at(end) && !at(TokenKind::EndOfFile)) {
				uint32_t before = current_.offset;
				elements.push_back(parse_assignment());
				height = std::max(height, last_height_);
				if (!accept(TokenKind::Comma) || current_.offset == before) {
					break;
				}
			}
			expect_close(end, open);
		} else {
			error_expected(fixed ? "'{'" : "'['");
		}
		vector->elements = to_list(elements);
		grow(height + 1);
		leave();
		return finish(vector);
	}
	New *create = node<New>(keyword.offset);
	create->callee = size ? parse_member_tail(size, false) : parse_call_member(false);
	uint32_t height = last_height_;
	if (at(TokenKind::LParen)) {
		uint32_t arguments_height = 0;
		create->arguments = parse_arguments(&arguments_height);
		create->has_arguments = true;
		height = std::max(height, arguments_height);
	}
	grow(height + 1);
	leave();
	return finish(create);
}

Expr *Parser::parse_parenthesized() {
	Token open = advance();
	Expr *inner;
	uint32_t height;
	{
		Restore allow_in(no_in_, false);
		Restore brackets(bracket_ends_initializer_, false);
		inner = parse_expression();
		height = last_height_;
	}
	expect_close(TokenKind::RParen, open);
	// No node of its own: the inner expression is marked and its range widened to the parentheses.
	inner->parenthesized = true;
	inner->offset = open.offset;
	if (previous_end_ > inner->end) {
		inner->end = previous_end_;
	}
	last_height_ = height;
	return inner;
}

Expr *Parser::parse_primary() {
	switch (current_.kind) {
		case TokenKind::Slash:
		case TokenKind::SlashAssign:
			return parse_regex();
		case TokenKind::Number:
		case TokenKind::String:
		case TokenKind::KwTrue:
		case TokenKind::KwFalse:
		case TokenKind::KwNull:
			return parse_literal_token();
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
		case TokenKind::LParen:
			return parse_parenthesized();
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
			while (at(TokenKind::At)) {
				advance();
			}
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

} // namespace kas3
