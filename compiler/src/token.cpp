#include "token.h"

namespace kas3 {

// Non-ASCII characters that are spacing, not part of a name. Returns the length in bytes (0 if p_text does
// not start with one) and whether it ends a line: no-break space, ideographic space, a byte-order mark,
// and the line and paragraph separators.
uint32_t unicode_space(const char *p_text, uint32_t p_left, bool *r_line_break) {
	const unsigned char *c = reinterpret_cast<const unsigned char *>(p_text);
	*r_line_break = false;
	if (p_left >= 2 && c[0] == 0xC2 && c[1] == 0xA0) {
		return 2;
	}
	if (p_left >= 3 && c[0] == 0xE3 && c[1] == 0x80 && c[2] == 0x80) {
		return 3;
	}
	if (p_left >= 3 && c[0] == 0xEF && c[1] == 0xBB && c[2] == 0xBF) {
		return 3;
	}
	if (p_left >= 3 && c[0] == 0xE2 && c[1] == 0x80 && (c[2] == 0xA8 || c[2] == 0xA9)) {
		*r_line_break = true;
		return 3;
	}
	return 0;
}

bool is_identifier_at(std::string_view p_text, uint32_t p_offset) {
	if (p_offset >= p_text.size() || !is_identifier_byte(p_text[p_offset])) {
		return false;
	}
	while (p_offset && (static_cast<unsigned char>(p_text[p_offset]) & 0xC0) == 0x80) {
		--p_offset;
	}
	bool line_break = false;
	return unicode_space(p_text.data() + p_offset, static_cast<uint32_t>(p_text.size()) - p_offset, &line_break) == 0;
}

bool is_identifier_byte(char p_char) {
	const auto byte = static_cast<unsigned char>(p_char);
	return (p_char >= 'a' && p_char <= 'z') || (p_char >= 'A' && p_char <= 'Z') ||
			(p_char >= '0' && p_char <= '9') || p_char == '_' || p_char == '$' || byte >= 0x80;
}

bool is_reserved_word(TokenKind p_kind) {
	return p_kind >= TokenKind::KwAs && p_kind <= TokenKind::KwWith;
}

bool is_identifier_like(TokenKind p_kind) {
	return p_kind == TokenKind::Identifier || is_reserved_word(p_kind);
}

TokenKind keyword_kind(std::string_view p_text) {
	// The reserved words are declared in alphabetical order, so a binary search over their spellings works.
	int low = static_cast<int>(TokenKind::KwAs);
	int high = static_cast<int>(TokenKind::KwWith);
	while (low <= high) {
		int middle = (low + high) / 2;
		std::string_view word = token_kind_spelling(static_cast<TokenKind>(middle));
		int order = word.compare(p_text);
		if (order == 0) {
			return static_cast<TokenKind>(middle);
		}
		if (order < 0) {
			low = middle + 1;
		} else {
			high = middle - 1;
		}
	}
	return TokenKind::Identifier;
}

} // namespace kas3
