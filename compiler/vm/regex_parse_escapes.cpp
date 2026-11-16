#include "regex_parser.h"

namespace kas3 {
namespace vm {
namespace regex {

namespace {

std::optional<uint32_t> hex_value(char16_t p_unit) {
	if (is_digit(p_unit)) {
		return p_unit - u'0';
	}
	if ((p_unit >= u'a' && p_unit <= u'f') || (p_unit >= u'A' && p_unit <= u'F')) {
		return (p_unit | 0x20) - u'a' + 10;
	}
	return std::nullopt;
}

bool is_class_letter(char16_t p_unit) {
	const char16_t lower = static_cast<char16_t>(p_unit | 0x20);
	return lower == u'd' || lower == u'w' || lower == u's';
}

} // namespace

std::optional<uint32_t> Parser::escape() {
	at_++;
	if (!more()) {
		fail("the pattern ends with a backslash");
		return std::nullopt;
	}
	const char16_t letter = text_[at_++];
	if (letter == u'b' || letter == u'B') {
		return leaf(Node::Boundary, 0, letter == u'B');
	}
	if (is_class_letter(letter)) {
		return set(class_ranges(letter), false);
	}
	if (letter >= u'1' && letter <= u'9') {
		at_--;
		return backreference();
	}
	return leaf(Node::Unit, character_escape(letter));
}

// `\1` and up: a group, if the pattern has that many; otherwise, as ECMAScript's Annex B and PCRE read it,
// up to three octal digits, or the digit itself.
std::optional<uint32_t> Parser::backreference() {
	const size_t start = at_;
	uint64_t value = 0;
	while (more() && is_digit(peek()) && value <= program_.groups) {
		value = value * 10 + (text_[at_++] - u'0');
	}
	if (value >= 1 && value <= program_.groups) {
		return leaf(Node::Backref, static_cast<uint32_t>(value));
	}
	at_ = start;
	return leaf(Node::Unit, character_escape(text_[at_++]));
}

// What `\` and the letter after it stand for, as one unit. Reads what follows the letter (`\x41`).
char16_t Parser::character_escape(char16_t p_letter) {
	const auto hex = [&](size_t p_digits) -> std::optional<char16_t> {
		uint32_t value = 0;
		for (size_t i = 0; i < p_digits; i++) {
			const std::optional<uint32_t> digit = hex_value(peek(i));
			if (!digit || at_ + i >= text_.size()) {
				return std::nullopt;
			}
			value = value * 16 + *digit;
		}
		at_ += p_digits;
		return static_cast<char16_t>(value);
	};
	switch (p_letter) {
		case u'f':
			return 0x0C;
		case u'n':
			return 0x0A;
		case u'r':
			return 0x0D;
		case u't':
			return 0x09;
		case u'v':
			return 0x0B;
		case u'c': {
			const char16_t control = peek();
			if (more() && ((control >= u'a' && control <= u'z') || (control >= u'A' && control <= u'Z'))) {
				at_++;
				return static_cast<char16_t>(control % 32);
			}
			at_--; // `\c` with no letter is a backslash, and the `c` is read after it.
			return u'\\';
		}
		case u'x':
			return hex(2).value_or(u'x');
		case u'u':
			return hex(4).value_or(u'u');
		default:
			break;
	}
	if (p_letter >= u'0' && p_letter <= u'7') {
		uint32_t value = p_letter - u'0';
		for (int i = 0; i < 2 && peek() >= u'0' && peek() <= u'7' && more() && value * 8 + (peek() - u'0') <= 0377; i++) {
			value = value * 8 + (text_[at_++] - u'0');
		}
		return static_cast<char16_t>(value);
	}
	return p_letter;
}

std::optional<uint32_t> Parser::character_class() {
	at_++;
	const bool negated = peek() == u'^' && more();
	at_ += negated ? 1 : 0;
	std::vector<Range> ranges;
	const auto take = [&](const ClassAtom &p_atom) {
		if (p_atom.escape) {
			const std::vector<Range> more_ranges = class_ranges(p_atom.unit);
			ranges.insert(ranges.end(), more_ranges.begin(), more_ranges.end());
		} else {
			ranges.push_back({ p_atom.unit, p_atom.unit });
		}
	};
	for (;;) {
		if (!more()) {
			fail("a class is not closed");
			return std::nullopt;
		}
		if (peek() == u']') {
			at_++;
			return set(std::move(ranges), negated);
		}
		const std::optional<ClassAtom> from = class_atom();
		if (!from) {
			return std::nullopt;
		}
		if (peek() != u'-' || peek(1) == u']' || at_ + 1 >= text_.size()) {
			take(*from);
			continue;
		}
		at_++;
		const std::optional<ClassAtom> to = class_atom();
		if (!to) {
			return std::nullopt;
		}
		if (from->escape || to->escape) {
			take(*from); // `[\d-z]`: as Annex B reads it, the dash is itself.
			take(ClassAtom{ u'-', false });
			take(*to);
		} else if (from->unit > to->unit) {
			fail("a range in a class goes backwards");
			return std::nullopt;
		} else {
			ranges.push_back({ from->unit, to->unit });
		}
	}
}

std::optional<ClassAtom> Parser::class_atom() {
	const char16_t unit = text_[at_++];
	if (unit != u'\\') {
		return ClassAtom{ unit, false };
	}
	if (!more()) {
		fail("the pattern ends with a backslash");
		return std::nullopt;
	}
	const char16_t letter = text_[at_++];
	if (is_class_letter(letter)) {
		return ClassAtom{ letter, true };
	}
	return ClassAtom{ letter == u'b' ? u'\b' : character_escape(letter), false };
}

} // namespace regex
} // namespace vm
} // namespace kas3
