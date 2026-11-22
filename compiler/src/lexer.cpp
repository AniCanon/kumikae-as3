#include "lexer.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <iterator>

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
	return is_identifier_byte(p_c);
}

static bool is_line_break(char p_c) {
	return p_c == '\n' || p_c == '\r';
}

Lexer::Lexer(const Source &p_source, Diagnostics &p_diagnostics, std::vector<TextRegion> *p_regions) :
		diagnostics_(p_diagnostics),
		text_(p_source.text().data()),
		size_(static_cast<uint32_t>(p_source.text().size())), regions_(p_regions) {
}

void Lexer::record_text(uint32_t p_start, bool p_closed) {
	if (!regions_) {
		return;
	}
	// Regex rescanning replaces any lookahead that interpreted its contents as code.
	while (!regions_->empty() && regions_->back().begin >= p_start) {
		regions_->pop_back();
	}
	regions_->push_back({ p_start, position_, p_closed });
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
		} else if ((unsigned char)c >= 0x80) {
			bool line_break;
			uint32_t length = unicode_space(text_ + position_, size_ - position_, &line_break);
			if (!length) {
				break;
			}
			newline = newline || line_break;
			position_ += length;
		} else if (c == '/' && position_ + 1 < size_ && text_[position_ + 1] == '/') {
			const uint32_t start = position_;
			while (position_ < size_ && !is_line_break(text_[position_])) {
				position_++;
			}
			record_text(start, false);
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
			record_text(start, closed);
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
	bool line_break;
	bool space_follows = position_ < size_ && (unsigned char)text_[position_] >= 0x80 &&
			unicode_space(text_ + position_, size_ - position_, &line_break) != 0;
	if (position_ < size_ && is_identifier_part(text_[position_]) && !space_follows) {
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
			record_text(p_start, true);
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
	record_text(p_start, false);
	return fail(p_start, p_newline_before, "unterminated string");
}

// Longest match over the punctuation and operators. Returns Error if the character starts none.
namespace {

// Every punctuation mark and operator, each one longer before any it begins with: the first that matches is
// the token.
struct Punctuation {
	std::string_view text;
	TokenKind kind;
};
constexpr Punctuation PUNCTUATION[] = {
	{ "(", TokenKind::LParen },
	{ ")", TokenKind::RParen },
	{ "{", TokenKind::LBrace },
	{ "}", TokenKind::RBrace },
	{ "[", TokenKind::LBracket },
	{ "]", TokenKind::RBracket },
	{ ";", TokenKind::Semicolon },
	{ ",", TokenKind::Comma },
	{ "?", TokenKind::Question },
	{ "@", TokenKind::At },
	{ "~", TokenKind::Tilde },
	{ "...", TokenKind::Ellipsis },
	{ "..", TokenKind::DotDot },
	{ ".<", TokenKind::DotLess },
	{ ".", TokenKind::Dot },
	{ "::", TokenKind::ColonColon },
	{ ":", TokenKind::Colon },
	{ "++", TokenKind::PlusPlus },
	{ "+=", TokenKind::PlusAssign },
	{ "+", TokenKind::Plus },
	{ "--", TokenKind::MinusMinus },
	{ "-=", TokenKind::MinusAssign },
	{ "-", TokenKind::Minus },
	{ "*=", TokenKind::StarAssign },
	{ "*", TokenKind::Star },
	{ "/=", TokenKind::SlashAssign },
	{ "/", TokenKind::Slash },
	{ "%=", TokenKind::PercentAssign },
	{ "%", TokenKind::Percent },
	{ "===", TokenKind::StrictEqual },
	{ "==", TokenKind::Equal },
	{ "=>", TokenKind::Arrow },
	{ "=", TokenKind::Assign },
	{ "!==", TokenKind::StrictNotEqual },
	{ "!=", TokenKind::NotEqual },
	{ "!", TokenKind::Bang },
	{ "<<=", TokenKind::ShiftLeftAssign },
	{ "<<", TokenKind::ShiftLeft },
	{ "<=", TokenKind::LessEqual },
	{ "<", TokenKind::Less },
	{ ">>>=", TokenKind::ShiftRightUnsignedAssign },
	{ ">>>", TokenKind::ShiftRightUnsigned },
	{ ">>=", TokenKind::ShiftRightAssign },
	{ ">>", TokenKind::ShiftRight },
	{ ">=", TokenKind::GreaterEqual },
	{ ">", TokenKind::Greater },
	{ "&&=", TokenKind::AmpAmpAssign },
	{ "&&", TokenKind::AmpAmp },
	{ "&=", TokenKind::AmpAssign },
	{ "&", TokenKind::Amp },
	{ "||=", TokenKind::PipePipeAssign },
	{ "||", TokenKind::PipePipe },
	{ "|=", TokenKind::PipeAssign },
	{ "|", TokenKind::Pipe },
	{ "^=", TokenKind::CaretAssign },
	{ "^", TokenKind::Caret },
};

constexpr uint8_t NO_MARK = 0xff;

// For each ASCII character, where the marks that begin with it start in PUNCTUATION (they are together), or
// NO_MARK: a mark is found by looking only at those.
constexpr std::array<uint8_t, 128> first_marks() {
	std::array<uint8_t, 128> first{};
	for (uint8_t &at : first) {
		at = NO_MARK;
	}
	for (size_t i = std::size(PUNCTUATION); i-- > 0;) {
		first[static_cast<unsigned char>(PUNCTUATION[i].text[0])] = static_cast<uint8_t>(i);
	}
	return first;
}
constexpr std::array<uint8_t, 128> FIRST_MARK = first_marks();

// For each ASCII character that is a mark and begins no longer one, its kind; TokenKind::Error for the others.
constexpr std::array<TokenKind, 128> only_marks() {
	std::array<TokenKind, 128> only{};
	for (TokenKind &kind : only) {
		kind = TokenKind::Error;
	}
	for (size_t i = 0; i < std::size(PUNCTUATION); i++) {
		const bool alone = PUNCTUATION[i].text.size() == 1 && (i == 0 || PUNCTUATION[i - 1].text[0] != PUNCTUATION[i].text[0]);
		if (alone) {
			only[static_cast<unsigned char>(PUNCTUATION[i].text[0])] = PUNCTUATION[i].kind;
		}
	}
	return only;
}
constexpr std::array<TokenKind, 128> ONLY_MARK = only_marks();

} // namespace

TokenKind Lexer::lex_punctuation() {
	const std::string_view rest(text_ + position_, size_ - position_);
	const unsigned char first = static_cast<unsigned char>(rest[0]);
	if (first < ONLY_MARK.size() && ONLY_MARK[first] != TokenKind::Error) {
		position_++;
		return ONLY_MARK[first]; // ( ) { } [ ] ; , and the like: no longer mark begins with them.
	}
	for (size_t i = first < FIRST_MARK.size() ? FIRST_MARK[first] : NO_MARK; i < std::size(PUNCTUATION) && PUNCTUATION[i].text[0] == rest[0]; i++) {
		const auto &[mark, kind] = PUNCTUATION[i];
		size_t same = 1; // A mark is one to four characters: compared here, not by a call.
		while (same < mark.size() && same < rest.size() && rest[same] == mark[same]) {
			same++;
		}
		if (same == mark.size()) {
			position_ += static_cast<uint32_t>(same);
			return kind;
		}
	}
	return TokenKind::Error;
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
			bool line_break;
			if ((unsigned char)text_[position_] >= 0x80 &&
					unicode_space(text_ + position_, size_ - position_, &line_break)) {
				break; // A Unicode space ends the name.
			}
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
			record_text(start, true);
			return make(TokenKind::Regex, start, p_slash.newline_before);
		}
		position_++;
	}
	record_text(start, false);
	return fail(start, p_slash.newline_before, "unterminated regular expression");
}

} // namespace kas3
