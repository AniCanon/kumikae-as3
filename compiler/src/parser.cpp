#include "parser.h"

namespace kas3 {

Parser::Parser(const Source &p_source, Arena &p_arena, Diagnostics &p_diagnostics) :
		source_(p_source), arena_(p_arena), diagnostics_(p_diagnostics), lexer_(p_source, p_diagnostics) {
	current_ = lexer_.next();
	note_current();
}

// The lexer has already reported an Error token, so the parser stays silent at that position.
void Parser::note_current() {
	if (current_.kind == TokenKind::Error) {
		has_error_offset_ = true;
		last_error_offset_ = current_.offset;
	}
}

// The second token is fetched lazily. If it were fetched eagerly, the lexer would run past a '/' before the
// parser had decided that it starts a regular expression.
const Token &Parser::peek2() {
	if (!has_next_) {
		next_ = lexer_.next();
		has_next_ = true;
	}
	return next_;
}

Token Parser::advance() {
	Token token = current_;
	previous_end_ = token.offset + token.length;
	if (aborted_) {
		return token; // Already at the end: see enter().
	}
	if (has_next_) {
		current_ = next_;
		has_next_ = false;
	} else {
		current_ = lexer_.next();
	}
	note_current();
	return token;
}

bool Parser::at_word(std::string_view p_word) const {
	return current_.kind == TokenKind::Identifier && text(current_) == p_word;
}

bool Parser::accept(TokenKind p_kind) {
	if (current_.kind != p_kind) {
		return false;
	}
	advance();
	return true;
}

std::string Parser::describe(const Token &p_token) const {
	if (p_token.kind == TokenKind::EndOfFile) {
		return "end of file";
	}
	std::string out = "'";
	out += text(p_token);
	out += "'";
	return out;
}

void Parser::error_at(uint32_t p_offset, std::string p_message, bool p_panic) {
	if (aborted_) {
		return;
	}
	if (p_panic) {
		panic_ = true;
	}
	if (has_error_offset_ && last_error_offset_ == p_offset) {
		return;
	}
	has_error_offset_ = true;
	last_error_offset_ = p_offset;
	diagnostics_.error(p_offset, std::move(p_message));
}

void Parser::error_expected(const char *p_what) {
	std::string message = "expected ";
	message += p_what;
	message += " but found ";
	message += describe(current_);
	error_at(current_.offset, std::move(message));
}

// On a mismatch, reports once and returns a zero-length token at the current position without consuming.
Token Parser::expect(TokenKind p_kind, const char *p_what) {
	if (current_.kind == p_kind) {
		return advance();
	}
	error_expected(p_what);
	return Token{ p_kind, current_.offset, 0, false };
}

bool Parser::enter() {
	if (depth_ >= MAX_DEPTH) {
		if (!too_deep_reported_) {
			too_deep_reported_ = true;
			diagnostics_.error(current_.offset, "code is nested too deeply");
		}
		// Stop here. Unwinding 256 levels would otherwise report every unclosed bracket on the way out, so
		// the parser jumps to the end of the file and says nothing more.
		aborted_ = true;
		uint32_t end = (uint32_t)source_.text().size();
		current_ = Token{ TokenKind::EndOfFile, end, 0, false };
		has_next_ = false;
		return false;
	}
	depth_++;
	return true;
}

void Parser::report_too_long(uint32_t p_offset) {
	if (!too_long_reported_) {
		too_long_reported_ = true;
		diagnostics_.error(p_offset, "expression is too long");
	}
}

// After an error: skip to a place where a statement or definition can start again.
void Parser::synchronize() {
	while (!at(TokenKind::EndOfFile)) {
		if (at(TokenKind::Semicolon)) {
			advance();
			return;
		}
		if (at(TokenKind::RBrace)) {
			return;
		}
		if (current_.newline_before && begins_statement(current_.kind)) {
			return;
		}
		advance();
	}
}

// Skips the rest of a statement the parser does not support.
void Parser::skip_to_statement_end() {
	while (!at(TokenKind::EndOfFile) && !at(TokenKind::RBrace)) {
		if (at(TokenKind::Semicolon)) {
			advance();
			return;
		}
		advance();
		if (current_.newline_before) {
			return;
		}
	}
}

// Skips inline XML: everything up to a `;` (left in place) or the end of the line.
void Parser::skip_xml() {
	advance();
	while (!at(TokenKind::EndOfFile) && !at(TokenKind::Semicolon) && !current_.newline_before) {
		advance();
	}
}

Expr *Parser::parse_single_expression() {
	Expr *expr = parse_expression();
	if (!at(TokenKind::EndOfFile)) {
		error_expected("end of file");
	}
	return expr;
}

Stmt *Parser::parse_single_statement() {
	Stmt *stmt = parse_statement();
	if (!at(TokenKind::EndOfFile)) {
		error_expected("end of file");
	}
	return stmt;
}

Expr *parse_expression_for_test(const Source &p_source, Arena &p_arena, Diagnostics &p_diagnostics) {
	Parser parser(p_source, p_arena, p_diagnostics);
	return parser.parse_single_expression();
}

Stmt *parse_statement_for_test(const Source &p_source, Arena &p_arena, Diagnostics &p_diagnostics) {
	Parser parser(p_source, p_arena, p_diagnostics);
	return parser.parse_single_statement();
}

} // namespace kas3
