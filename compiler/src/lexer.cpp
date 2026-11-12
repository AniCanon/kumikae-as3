#include "lexer.h"

#include <cstdio>

namespace kas3 {

static bool is_digit(char p_c) {
	return p_c >= '0' && p_c <= '9';
}

static bool is_hex_digit(char p_c) {
	return is_digit(p_c) || (p_c >= 'a' && p_c <= 'f') || (p_c >= 'A' && p_c <= 'F');
}

// Letters, '_', '$', and any byte of a non-ASCII character (accepted without decoding).
static bool is_identifier_start(char p_c) {
	return (p_c >= 'a' && p_c <= 'z') || (p_c >= 'A' && p_c <= 'Z') || p_c == '_' || p_c == '$' ||
			(unsigned char)p_c >= 0x80;
}

static bool is_identifier_part(char p_c) {
	return is_identifier_start(p_c) || is_digit(p_c);
}

static bool is_line_break(char p_c) {
	return p_c == '\n' || p_c == '\r';
}

Lexer::Lexer(const Source &p_source, Diagnostics &p_diagnostics) :
		diagnostics_(p_diagnostics),
		text_(p_source.text().data()),
		size_((uint32_t)p_source.text().size()) {
}

std::string_view Lexer::text(const Token &p_token) const {
	return std::string_view(text_ + p_token.offset, p_token.length);
}

Token Lexer::make(TokenKind p_kind, uint32_t p_start, bool p_newline_before) const {
	return Token{ p_kind, p_start, position_ - p_start, p_newline_before };
}

Token Lexer::fail(uint32_t p_start, bool p_newline_before, const char *p_message) {
	diagnostics_.error(p_start, p_message);
	return make(TokenKind::Error, p_start, p_newline_before);
}

// Skips whitespace and comments. Returns whether a line break was crossed. An unterminated block comment
// comes back through r_error (its kind is set to Error); otherwise r_error's kind is EndOfFile.
bool Lexer::skip_trivia(Token *r_error) {
	bool newline = false;
	r_error->kind = TokenKind::EndOfFile;
	while (position_ < size_) {
		char c = text_[position_];
		if (c == ' ' || c == '\t' || c == '\f' || c == '\v') {
			position_++;
		} else if (is_line_break(c)) {
			newline = true;
			position_++;
		} else if (c == '/' && position_ + 1 < size_ && text_[position_ + 1] == '/') {
			while (position_ < size_ && !is_line_break(text_[position_])) {
				position_++;
			}
		} else if (c == '/' && position_ + 1 < size_ && text_[position_ + 1] == '*') {
			uint32_t start = position_;
			position_ += 2;
			bool closed = false;
			while (position_ < size_) {
				if (text_[position_] == '*' && position_ + 1 < size_ && text_[position_ + 1] == '/') {
					position_ += 2;
					closed = true;
					break;
				}
				if (is_line_break(text_[position_])) {
					newline = true;
				}
				position_++;
			}
			if (!closed) {
				*r_error = fail(start, newline, "unterminated comment");
				return newline;
			}
		} else {
			break;
		}
	}
	return newline;
}

Token Lexer::lex_number(uint32_t p_start, bool p_newline_before) {
	bool valid = true;
	if (text_[position_] == '0' && position_ + 1 < size_ && (text_[position_ + 1] == 'x' || text_[position_ + 1] == 'X')) {
		position_ += 2;
		uint32_t digits = position_;
		while (position_ < size_ && is_hex_digit(text_[position_])) {
			position_++;
		}
		valid = position_ > digits;
	} else {
		while (position_ < size_ && is_digit(text_[position_])) {
			position_++;
		}
		// A dot belongs to the number only when a digit follows it, so that "1.toString" is a member access.
		if (position_ + 1 < size_ && text_[position_] == '.' && is_digit(text_[position_ + 1])) {
			position_++;
			while (position_ < size_ && is_digit(text_[position_])) {
				position_++;
			}
		}
		if (position_ < size_ && (text_[position_] == 'e' || text_[position_] == 'E')) {
			uint32_t after = position_ + 1;
			if (after < size_ && (text_[after] == '+' || text_[after] == '-')) {
				after++;
			}
			if (after < size_ && is_digit(text_[after])) {
				position_ = after;
				while (position_ < size_ && is_digit(text_[position_])) {
					position_++;
				}
			}
		}
	}
	// "12abc", "0x" and "1e" are one bad token, not a number followed by a name.
	if (position_ < size_ && is_identifier_part(text_[position_])) {
		valid = false;
		while (position_ < size_ && is_identifier_part(text_[position_])) {
			position_++;
		}
	}
	if (!valid) {
		return fail(p_start, p_newline_before, "invalid number");
	}
	return make(TokenKind::Number, p_start, p_newline_before);
}

Token Lexer::lex_string(uint32_t p_start, bool p_newline_before) {
	char quote = text_[position_++];
	while (position_ < size_) {
		char c = text_[position_];
		if (c == quote) {
			position_++;
			return make(TokenKind::String, p_start, p_newline_before);
		}
		if (is_line_break(c)) {
			break;
		}
		if (c == '\\' && position_ + 1 < size_) {
			// An escaped character, or a backslash before a line break, which continues the string.
			if (text_[position_ + 1] == '\r' && position_ + 2 < size_ && text_[position_ + 2] == '\n') {
				position_++;
			}
			position_ += 2;
			continue;
		}
		position_++;
	}
	return fail(p_start, p_newline_before, "unterminated string");
}

// Longest match over the punctuation and operators. Returns Error if the character starts none.
TokenKind Lexer::lex_punctuation() {
	auto follows = [this](const char *p_rest) {
		uint32_t at = position_ + 1;
		for (const char *c = p_rest; *c; c++, at++) {
			if (at >= size_ || text_[at] != *c) {
				return false;
			}
		}
		return true;
	};
	auto take = [this](uint32_t p_length, TokenKind p_kind) {
		position_ += p_length;
		return p_kind;
	};
	switch (text_[position_]) {
		case '(':
			return take(1, TokenKind::LParen);
		case ')':
			return take(1, TokenKind::RParen);
		case '{':
			return take(1, TokenKind::LBrace);
		case '}':
			return take(1, TokenKind::RBrace);
		case '[':
			return take(1, TokenKind::LBracket);
		case ']':
			return take(1, TokenKind::RBracket);
		case ';':
			return take(1, TokenKind::Semicolon);
		case ',':
			return take(1, TokenKind::Comma);
		case '?':
			return take(1, TokenKind::Question);
		case '@':
			return take(1, TokenKind::At);
		case '~':
			return take(1, TokenKind::Tilde);
		case '.':
			if (follows("..")) {
				return take(3, TokenKind::Ellipsis);
			}
			if (follows(".")) {
				return take(2, TokenKind::DotDot);
			}
			if (follows("<")) {
				return take(2, TokenKind::DotLess);
			}
			return take(1, TokenKind::Dot);
		case ':':
			return follows(":") ? take(2, TokenKind::ColonColon) : take(1, TokenKind::Colon);
		case '+':
			if (follows("+")) {
				return take(2, TokenKind::PlusPlus);
			}
			return follows("=") ? take(2, TokenKind::PlusAssign) : take(1, TokenKind::Plus);
		case '-':
			if (follows("-")) {
				return take(2, TokenKind::MinusMinus);
			}
			return follows("=") ? take(2, TokenKind::MinusAssign) : take(1, TokenKind::Minus);
		case '*':
			return follows("=") ? take(2, TokenKind::StarAssign) : take(1, TokenKind::Star);
		case '/':
			return follows("=") ? take(2, TokenKind::SlashAssign) : take(1, TokenKind::Slash);
		case '%':
			return follows("=") ? take(2, TokenKind::PercentAssign) : take(1, TokenKind::Percent);
		case '=':
			if (follows("==")) {
				return take(3, TokenKind::StrictEqual);
			}
			return follows("=") ? take(2, TokenKind::Equal) : take(1, TokenKind::Assign);
		case '!':
			if (follows("==")) {
				return take(3, TokenKind::StrictNotEqual);
			}
			return follows("=") ? take(2, TokenKind::NotEqual) : take(1, TokenKind::Bang);
		case '<':
			if (follows("<=")) {
				return take(3, TokenKind::ShiftLeftAssign);
			}
			if (follows("<")) {
				return take(2, TokenKind::ShiftLeft);
			}
			return follows("=") ? take(2, TokenKind::LessEqual) : take(1, TokenKind::Less);
		case '>':
			if (follows(">>=")) {
				return take(4, TokenKind::ShiftRightUnsignedAssign);
			}
			if (follows(">>")) {
				return take(3, TokenKind::ShiftRightUnsigned);
			}
			if (follows(">=")) {
				return take(3, TokenKind::ShiftRightAssign);
			}
			if (follows(">")) {
				return take(2, TokenKind::ShiftRight);
			}
			return follows("=") ? take(2, TokenKind::GreaterEqual) : take(1, TokenKind::Greater);
		case '&':
			if (follows("&=")) {
				return take(3, TokenKind::AmpAmpAssign);
			}
			if (follows("&")) {
				return take(2, TokenKind::AmpAmp);
			}
			return follows("=") ? take(2, TokenKind::AmpAssign) : take(1, TokenKind::Amp);
		case '|':
			if (follows("|=")) {
				return take(3, TokenKind::PipePipeAssign);
			}
			if (follows("|")) {
				return take(2, TokenKind::PipePipe);
			}
			return follows("=") ? take(2, TokenKind::PipeAssign) : take(1, TokenKind::Pipe);
		case '^':
			return follows("=") ? take(2, TokenKind::CaretAssign) : take(1, TokenKind::Caret);
		default:
			return TokenKind::Error;
	}
}

Token Lexer::next() {
	Token comment_error;
	bool newline = skip_trivia(&comment_error);
	if (comment_error.kind == TokenKind::Error) {
		return comment_error;
	}
	uint32_t start = position_;
	if (position_ >= size_) {
		return Token{ TokenKind::EndOfFile, size_, 0, newline };
	}
	char c = text_[position_];
	if (is_identifier_start(c)) {
		while (position_ < size_ && is_identifier_part(text_[position_])) {
			position_++;
		}
		return make(keyword_kind(std::string_view(text_ + start, position_ - start)), start, newline);
	}
	if (is_digit(c) || (c == '.' && position_ + 1 < size_ && is_digit(text_[position_ + 1]))) {
		return lex_number(start, newline);
	}
	if (c == '"' || c == '\'') {
		return lex_string(start, newline);
	}
	TokenKind kind = lex_punctuation();
	if (kind != TokenKind::Error) {
		return make(kind, start, newline);
	}
	position_++;
	char message[48];
	if ((unsigned char)c < 0x20 || c == 0x7F) {
		std::snprintf(message, sizeof(message), "unexpected character '\\x%02X'", (unsigned)(unsigned char)c);
	} else {
		std::snprintf(message, sizeof(message), "unexpected character '%c'", c);
	}
	return fail(start, newline, message);
}

Token Lexer::rescan_as_regex(const Token &p_slash) {
	uint32_t start = p_slash.offset;
	position_ = start + 1;
	bool in_class = false;
	while (position_ < size_) {
		char c = text_[position_];
		if (is_line_break(c)) {
			break;
		}
		if (c == '\\' && position_ + 1 < size_ && !is_line_break(text_[position_ + 1])) {
			position_ += 2;
			continue;
		}
		if (c == '[') {
			in_class = true;
		} else if (c == ']') {
			in_class = false;
		} else if (c == '/' && !in_class) {
			position_++;
			while (position_ < size_ && is_identifier_part(text_[position_])) {
				position_++;
			}
			return make(TokenKind::Regex, start, p_slash.newline_before);
		}
		position_++;
	}
	return fail(start, p_slash.newline_before, "unterminated regular expression");
}

} // namespace kas3
