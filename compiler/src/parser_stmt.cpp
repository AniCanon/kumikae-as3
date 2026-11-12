// Statements, variable declarations and functions.
#include "parser.h"

namespace kas3 {

namespace {

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

// Where the parser may pick up again after an error, when the token starts a line.
bool Parser::begins_statement(TokenKind p_kind) {
	switch (p_kind) {
		case TokenKind::KwVar:
		case TokenKind::KwConst:
		case TokenKind::KwFunction:
		case TokenKind::KwClass:
		case TokenKind::KwInterface:
		case TokenKind::KwIf:
		case TokenKind::KwFor:
		case TokenKind::KwWhile:
		case TokenKind::KwDo:
		case TokenKind::KwSwitch:
		case TokenKind::KwReturn:
		case TokenKind::KwBreak:
		case TokenKind::KwContinue:
		case TokenKind::KwThrow:
		case TokenKind::KwTry:
		case TokenKind::KwImport:
		case TokenKind::KwPackage:
		case TokenKind::KwPublic:
		case TokenKind::KwPrivate:
		case TokenKind::KwProtected:
		case TokenKind::KwInternal:
			return true;
		default:
			return false;
	}
}

// A statement ends at `;`, or before `}` or the end of the file, or at a line break.
void Parser::end_statement() {
	if (accept(TokenKind::Semicolon)) {
		return;
	}
	if (at(TokenKind::RBrace) || at(TokenKind::EndOfFile) || current_.newline_before) {
		return;
	}
	// The statement itself is complete, so the next one starts right here: report without resynchronising.
	std::string message = "expected ';' but found ";
	message += describe(current_);
	error_at(current_.offset, std::move(message), false);
}

Block *Parser::parse_block() {
	Block *block = node<Block>(current_.offset);
	Token open = expect(TokenKind::LBrace, "'{'");
	if (!open.length) {
		return finish(block);
	}
	int level = brace_depth_;
	std::vector<Stmt *> statements;
	while (!at(TokenKind::RBrace) && !at(TokenKind::EndOfFile)) {
		uint32_t before = current_.offset;
		panic_ = false;
		statements.push_back(parse_statement());
		if (panic_) {
			synchronize(level);
			panic_ = false;
		}
		// Whatever happened, move forward, so that no input can make this loop spin.
		if (current_.offset == before && !at(TokenKind::RBrace) && !at(TokenKind::EndOfFile)) {
			advance();
		}
	}
	expect_close(TokenKind::RBrace, open);
	block->statements = to_list(statements);
	return finish(block);
}

Block *Parser::parse_function_body() {
	Restore allow_in(no_in_, false);
	Restore brackets(bracket_ends_initializer_, false);
	return parse_block();
}

Stmt *Parser::parse_statement() {
	if (!enter()) {
		return finish(node<Empty>(current_.offset));
	}
	statement_depth_++;
	Stmt *statement = parse_statement_inner();
	statement_depth_--;
	leave();
	return statement;
}

// Constructs left out of the first version. Each is reported once and then skipped.
bool Parser::parse_unsupported_statement() {
	if (at(TokenKind::KwUse)) {
		error_at(current_.offset, "namespaces are not supported", false);
	} else if (at_word("namespace") && peek2().kind == TokenKind::Identifier) {
		error_at(current_.offset, "namespaces are not supported", false);
	} else if (at_word("include") && peek2().kind == TokenKind::String) {
		error_at(current_.offset, "'include' is not supported", false);
	} else if (at(TokenKind::KwDefault) && peek2().kind == TokenKind::Identifier && text(peek2()) == "xml") {
		error_at(current_.offset, "E4X (XML syntax) is not supported", false);
	} else {
		return false;
	}
	skip_to_statement_end();
	return true;
}

Import *Parser::parse_import() {
	Import *import = node<Import>(advance().offset);
	import->name = parse_dotted_name("a name to import", &import->wildcard, &import->name_offsets);
	end_statement();
	return finish(import);
}

Stmt *Parser::parse_statement_inner() {
	uint32_t start = current_.offset;
	switch (current_.kind) {
		case TokenKind::LBrace:
			return parse_block();
		case TokenKind::Semicolon:
			advance();
			return finish(node<Empty>(start));
		case TokenKind::KwVar:
		case TokenKind::KwConst: {
			VarStmt *statement = node<VarStmt>(start);
			statement->decl = parse_variable_declaration(Modifiers(), List<Metadata>(), true);
			end_statement();
			return finish(statement);
		}
		case TokenKind::KwIf:
			return parse_if();
		case TokenKind::KwFor:
			return parse_for();
		case TokenKind::KwWhile: {
			While *loop = node<While>(advance().offset);
			Token open = expect(TokenKind::LParen, "'('");
			loop->condition = parse_expression();
			expect_close(TokenKind::RParen, open);
			loop->body = parse_statement();
			return finish(loop);
		}
		case TokenKind::KwDo: {
			DoWhile *loop = node<DoWhile>(advance().offset);
			loop->body = parse_statement();
			expect(TokenKind::KwWhile, "'while'");
			Token open = expect(TokenKind::LParen, "'('");
			loop->condition = parse_expression();
			expect_close(TokenKind::RParen, open);
			end_statement();
			return finish(loop);
		}
		case TokenKind::KwSwitch:
			return parse_switch();
		case TokenKind::KwBreak: {
			Break *jump = node<Break>(advance().offset);
			if (at(TokenKind::Identifier) && !current_.newline_before) {
				jump->label_offset = current_.offset;
				jump->label = text(advance());
			}
			end_statement();
			return finish(jump);
		}
		case TokenKind::KwContinue: {
			Continue *jump = node<Continue>(advance().offset);
			if (at(TokenKind::Identifier) && !current_.newline_before) {
				jump->label_offset = current_.offset;
				jump->label = text(advance());
			}
			end_statement();
			return finish(jump);
		}
		case TokenKind::KwReturn: {
			Return *result = node<Return>(advance().offset);
			// A value is taken only from the same line: `return` then a line break returns nothing.
			if (!at(TokenKind::Semicolon) && !at(TokenKind::RBrace) && !at(TokenKind::EndOfFile) &&
					!current_.newline_before) {
				result->value = parse_expression();
			}
			end_statement();
			return finish(result);
		}
		case TokenKind::KwThrow: {
			Throw *raise = node<Throw>(advance().offset);
			if (at(TokenKind::Semicolon) || at(TokenKind::RBrace) || at(TokenKind::EndOfFile) ||
					current_.newline_before) {
				error_at(start, "expected an expression after 'throw'", false);
			} else {
				raise->value = parse_expression();
			}
			end_statement();
			return finish(raise);
		}
		case TokenKind::KwTry:
			return parse_try();
		case TokenKind::KwFunction:
			// A named function nested in a function. Without a name it is a function expression.
			if (peek2().kind == TokenKind::Identifier) {
				FunctionStmt *statement = node<FunctionStmt>(advance().offset);
				statement->function = parse_function_rest(start, Modifiers(), List<Metadata>(), true, false, true);
				// The nested function's height counts toward the body it sits in.
				last_height_ = function_height_ + 1;
				note_height();
				return finish(statement);
			}
			break;
		case TokenKind::KwWith: {
			error_at(start, "the 'with' statement is not supported", false);
			advance();
			if (at(TokenKind::LParen)) {
				Token open = advance();
				parse_expression();
				expect_close(TokenKind::RParen, open);
			}
			parse_statement();
			return finish(node<Empty>(start));
		}
		case TokenKind::KwImport: {
			ImportStmt *statement = node<ImportStmt>(start);
			statement->import = parse_import();
			return finish(statement);
		}
		case TokenKind::Identifier:
			if (peek2().kind == TokenKind::Colon) {
				Labeled *labeled = node<Labeled>(start);
				labeled->label = text(advance());
				advance();
				labeled->body = parse_statement();
				return finish(labeled);
			}
			break;
		default:
			break;
	}
	if (parse_unsupported_statement()) {
		return finish(node<Empty>(start));
	}
	ExprStmt *statement = node<ExprStmt>(start);
	statement->expr = parse_expression();
	end_statement();
	return finish(statement);
}

Stmt *Parser::parse_if() {
	If *branch = node<If>(advance().offset);
	Token open = expect(TokenKind::LParen, "'('");
	branch->condition = parse_expression();
	expect_close(TokenKind::RParen, open);
	branch->then_branch = parse_statement();
	if (accept(TokenKind::KwElse)) {
		branch->else_branch = parse_statement();
	}
	return finish(branch);
}

Stmt *Parser::parse_for() {
	uint32_t start = advance().offset;
	bool each = false;
	if (at_word("each")) {
		advance();
		each = true;
	}
	Token open = expect(TokenKind::LParen, "'('");

	// The first clause is parsed with `in` switched off as an operator, so that `for (k in o)` is seen.
	VarDecl *declaration = nullptr;
	Expr *first = nullptr;
	uint32_t first_offset = current_.offset;
	if (at(TokenKind::KwVar) || at(TokenKind::KwConst)) {
		declaration = parse_variable_declaration(Modifiers(), List<Metadata>(), false);
	} else if (!at(TokenKind::Semicolon)) {
		Restore no_in(no_in_, true);
		first = parse_expression();
	}

	if (at(TokenKind::KwIn)) {
		ForIn *loop = node<ForIn>(start);
		loop->each = each;
		if (declaration) {
			if (declaration->declarators.count != 1 || declaration->declarators[0].initializer) {
				error_at(first_offset, "invalid left-hand side in for-in", false);
			}
			loop->declaration = declaration;
		} else {
			if (!first || (first->kind != NodeKind::Name && first->kind != NodeKind::Member &&
								  first->kind != NodeKind::Index)) {
				error_at(first_offset, "invalid left-hand side in for-in", false);
			}
			loop->target = first;
		}
		advance();
		loop->collection = parse_expression();
		expect_close(TokenKind::RParen, open);
		loop->body = parse_statement();
		return finish(loop);
	}

	For *loop = node<For>(start);
	if (each) {
		error_expected("'in'");
	}
	if (declaration) {
		VarStmt *init = node<VarStmt>(declaration->offset);
		init->decl = declaration;
		init->end = declaration->end;
		loop->init = init;
	} else if (first) {
		ExprStmt *init = node<ExprStmt>(first->offset);
		init->expr = first;
		init->end = first->end;
		loop->init = init;
	}
	expect(TokenKind::Semicolon, "';'");
	if (!at(TokenKind::Semicolon)) {
		loop->condition = parse_expression();
	}
	expect(TokenKind::Semicolon, "';'");
	if (!at(TokenKind::RParen)) {
		loop->update = parse_expression();
	}
	expect_close(TokenKind::RParen, open);
	loop->body = parse_statement();
	return finish(loop);
}

Stmt *Parser::parse_switch() {
	Switch *choice = node<Switch>(advance().offset);
	Token open_paren = expect(TokenKind::LParen, "'('");
	choice->subject = parse_expression();
	expect_close(TokenKind::RParen, open_paren);
	std::vector<SwitchCase> cases;
	bool has_default = false;
	Token open = expect(TokenKind::LBrace, "'{'");
	if (open.length) {
		int level = brace_depth_;
		while (!at(TokenKind::RBrace) && !at(TokenKind::EndOfFile)) {
			SwitchCase clause;
			clause.offset = current_.offset;
			if (accept(TokenKind::KwCase)) {
				clause.test = parse_expression();
			} else if (at(TokenKind::KwDefault)) {
				if (has_default) {
					error_at(current_.offset, "more than one 'default' in switch", false);
				}
				has_default = true;
				clause.is_default = true;
				advance();
			} else {
				error_expected("'case' or 'default'");
				synchronize(level);
				panic_ = false;
				if (current_.offset == clause.offset && !at(TokenKind::RBrace) && !at(TokenKind::EndOfFile)) {
					advance();
				}
				continue;
			}
			expect(TokenKind::Colon, "':'");
			std::vector<Stmt *> body;
			while (!at(TokenKind::KwCase) && !at(TokenKind::KwDefault) && !at(TokenKind::RBrace) &&
					!at(TokenKind::EndOfFile)) {
				uint32_t before = current_.offset;
				panic_ = false;
				body.push_back(parse_statement());
				if (panic_) {
					synchronize(level);
					panic_ = false;
				}
				if (current_.offset == before && !at(TokenKind::RBrace) && !at(TokenKind::EndOfFile)) {
					advance();
				}
			}
			clause.body = to_list(body);
			clause.end = previous_end_;
			cases.push_back(clause);
		}
		expect_close(TokenKind::RBrace, open);
	}
	choice->cases = to_list(cases);
	return finish(choice);
}

Stmt *Parser::parse_try() {
	Try *attempt = node<Try>(advance().offset);
	attempt->body = parse_block();
	std::vector<CatchClause> catches;
	while (at(TokenKind::KwCatch)) {
		CatchClause clause;
		clause.offset = advance().offset;
		Token open = expect(TokenKind::LParen, "'('");
		if (at(TokenKind::Identifier)) {
			clause.name = text(advance());
		} else {
			error_expected("a name for the caught error");
		}
		if (accept(TokenKind::Colon)) {
			clause.type = parse_type();
		}
		expect_close(TokenKind::RParen, open);
		clause.body = parse_block();
		clause.end = previous_end_;
		catches.push_back(clause);
	}
	attempt->catches = to_list(catches);
	if (accept(TokenKind::KwFinally)) {
		attempt->finally_block = parse_block();
	}
	if (catches.empty() && !attempt->finally_block) {
		error_at(attempt->offset, "'try' needs a 'catch' or 'finally'", false);
	}
	return finish(attempt);
}

// Called at `var` or `const`. Parses the declarators but not what ends the statement.
VarDecl *Parser::parse_variable_declaration(Modifiers p_modifiers, List<Metadata> p_metadata, bool p_allow_in) {
	VarDecl *declaration = node<VarDecl>(current_.offset);
	declaration->is_const = advance().kind == TokenKind::KwConst;
	declaration->modifiers = p_modifiers;
	declaration->metadata = p_metadata;
	std::vector<Declarator> declarators;
	for (;;) {
		Declarator declarator;
		declarator.offset = current_.offset;
		if (at(TokenKind::Identifier)) {
			declarator.name = text(advance());
		} else {
			error_expected("a variable name");
			break;
		}
		if (accept(TokenKind::Colon)) {
			declarator.type = parse_type();
		}
		if (accept(TokenKind::Assign)) {
			Restore no_in(no_in_, !p_allow_in);
			declarator.initializer = parse_assignment();
		}
		declarator.end = previous_end_;
		declarators.push_back(declarator);
		if (!accept(TokenKind::Comma)) {
			break;
		}
	}
	declaration->declarators = to_list(declarators);
	return finish(declaration);
}

// Called just after `function`. Handles `get`/`set`, the name, parameters, return type and body.
FunctionDecl *Parser::parse_function_rest(uint32_t p_offset, Modifiers p_modifiers, List<Metadata> p_metadata,
		bool p_body_expected, bool p_allow_accessor, bool p_name_required) {
	FunctionDecl *function = node<FunctionDecl>(p_offset);
	function->modifiers = p_modifiers;
	function->metadata = p_metadata;
	// `function get name()` is an accessor; `function get()` is a function called `get`.
	if (p_allow_accessor && (at_word("get") || at_word("set")) && is_identifier_like(peek2().kind)) {
		function->accessor = text(advance()) == "get" ? FunctionDecl::Getter : FunctionDecl::Setter;
	}
	if (at(TokenKind::Identifier) || (function->accessor != FunctionDecl::None && is_identifier_like(current_.kind))) {
		function->name_offset = current_.offset;
		function->name = text(advance());
	} else if (p_name_required) {
		error_expected("a function name");
	}

	std::vector<Parameter> parameters;
	uint32_t defaults_height = 0; // Tallest default value, which hangs below this function in the tree.
	Token open = expect(TokenKind::LParen, "'('");
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
	return finish(function);
}

} // namespace kas3
