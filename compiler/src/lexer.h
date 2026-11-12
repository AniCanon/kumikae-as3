// Source text to tokens.
#pragma once

#include "diagnostics.h"
#include "source.h"
#include "token.h"

#include <string_view>

namespace kas3 {

class Lexer {
	Diagnostics &diagnostics_;
	const char *text_;
	uint32_t size_;
	uint32_t position_ = 0;

	bool skip_trivia(Token *r_error);
	Token make(TokenKind p_kind, uint32_t p_start, bool p_newline_before) const;
	Token fail(uint32_t p_start, bool p_newline_before, const char *p_message);
	Token lex_number(uint32_t p_start, bool p_newline_before);
	Token lex_string(uint32_t p_start, bool p_newline_before);
	TokenKind lex_punctuation();

public:
	Lexer(const Source &p_source, Diagnostics &p_diagnostics);

	// Returns EndOfFile forever once the text is used up.
	Token next();
	// Re-reads a '/' or '/=' token as the start of a regular expression. The lexer rewinds to the slash, so
	// anything it produced after that token is discarded.
	Token rescan_as_regex(const Token &p_slash);
	std::string_view text(const Token &p_token) const;
};

} // namespace kas3
