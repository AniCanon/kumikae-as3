#include "token.h"

namespace kas3 {

static const char *const NAMES[] = {
#define X(kind, name) name,
	KAS3_TOKENS(X)
#undef X
};

const char *token_kind_name(TokenKind p_kind) {
	return NAMES[(int)p_kind];
}

std::string_view token_kind_spelling(TokenKind p_kind) {
	std::string_view name = NAMES[(int)p_kind];
	if (name.size() >= 2 && name.front() == '\'' && name.back() == '\'') {
		return name.substr(1, name.size() - 2);
	}
	return std::string_view();
}

bool is_reserved_word(TokenKind p_kind) {
	return p_kind >= TokenKind::KwAs && p_kind <= TokenKind::KwWith;
}

bool is_identifier_like(TokenKind p_kind) {
	return p_kind == TokenKind::Identifier || is_reserved_word(p_kind);
}

TokenKind keyword_kind(std::string_view p_text) {
	// The reserved words are declared in alphabetical order, so a binary search over their spellings works.
	int low = (int)TokenKind::KwAs;
	int high = (int)TokenKind::KwWith;
	while (low <= high) {
		int middle = (low + high) / 2;
		std::string_view word = token_kind_spelling((TokenKind)middle);
		int order = word.compare(p_text);
		if (order == 0) {
			return (TokenKind)middle;
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
