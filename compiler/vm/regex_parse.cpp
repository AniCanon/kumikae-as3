#include "regex_tree.h"

#include <algorithm>
#include <array>

namespace kas3 {
namespace vm {
namespace regex {

namespace {

using Range = std::pair<char16_t, char16_t>;

bool is_digit(char16_t p_unit) {
	return p_unit >= u'0' && p_unit <= u'9';
}

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

bool is_name_unit(char16_t p_unit) {
	return is_digit(p_unit) || (p_unit >= u'A' && p_unit <= u'Z') || (p_unit >= u'a' && p_unit <= u'z') || p_unit == u'_';
}

std::string narrow(std::u16string_view p_text) {
	std::string out;
	for (const char16_t unit : p_text) {
		out += unit < 0x80 ? static_cast<char>(unit) : '?';
	}
	return out;
}

// How many groups capture, counted before the pattern is read: `\2` refers back to the second group even
// when it comes before that group.
uint32_t count_groups(std::u16string_view p_text) {
	uint32_t count = 0;
	bool in_class = false;
	for (size_t at = 0; at < p_text.size(); at++) {
		const char16_t unit = p_text[at];
		if (unit == u'\\') {
			at++;
		} else if (in_class) {
			in_class = unit != u']';
		} else if (unit == u'[') {
			in_class = true;
		} else if (unit == u'(') {
			const std::u16string_view rest = p_text.substr(at + 1);
			const bool named = rest.substr(0, 3) == u"?P<" || (rest.substr(0, 2) == u"?<" && rest.size() > 2 && rest[2] != u'=' && rest[2] != u'!');
			count += rest.empty() || rest[0] != u'?' || named ? 1 : 0;
		}
	}
	return count;
}

// One unit of a class, or a class escape in it.
struct ClassAtom {
	char16_t unit = 0;
	bool escape = false; // \d and the others: `unit` is the letter.
};

class Parser {
	std::u16string_view text_;
	Program &program_;
	Tree tree_;
	size_t at_ = 0;
	uint32_t depth_ = 0;
	uint32_t next_group_ = 1;
	std::string error_;
	struct NameUse {
		uint32_t node;
		std::u16string name;
	};
	std::vector<NameUse> name_uses_;

	bool fail(const std::string &p_why) {
		if (error_.empty()) {
			error_ = p_why;
		}
		return false;
	}
	bool more() const { return at_ < text_.size(); }
	char16_t peek(size_t p_ahead = 0) const { return at_ + p_ahead < text_.size() ? text_[at_ + p_ahead] : 0; }
	bool extended() const { return (program_.flags & EXTENDED) != 0; }

	uint32_t add(Node p_node) {
		tree_.nodes.push_back(std::move(p_node));
		return static_cast<uint32_t>(tree_.nodes.size() - 1);
	}
	uint32_t leaf(Node::Kind p_kind, uint32_t p_value = 0, bool p_flag = false) {
		Node node;
		node.kind = p_kind;
		node.value = p_value;
		node.flag = p_flag;
		return add(std::move(node));
	}
	uint32_t set(std::vector<Range> p_ranges, bool p_negated) {
		program_.sets.push_back(Set{ std::move(p_ranges) });
		return leaf(Node::Set, static_cast<uint32_t>(program_.sets.size() - 1), p_negated);
	}

	// In extended mode, white space and comments from `#` to the end of the line are not the pattern's.
	void skip_extended() {
		while (extended() && more()) {
			const char16_t unit = peek();
			if (unit == u'#') {
				while (more() && peek() != u'\n') {
					at_++;
				}
			} else if (unit != u' ' && (unit < 0x09 || unit > 0x0D)) {
				return;
			}
			at_++;
		}
	}

	std::optional<uint32_t> disjunction();
	std::optional<uint32_t> alternative();
	std::optional<uint32_t> atom();
	std::optional<uint32_t> group();
	std::optional<uint32_t> special_group();
	std::optional<uint32_t> group_body(Node p_node);
	std::optional<std::u16string> group_name(char16_t p_end);
	std::optional<uint32_t> escape();
	std::optional<uint32_t> backreference();
	char16_t character_escape(char16_t p_letter);
	std::optional<uint32_t> character_class();
	std::optional<ClassAtom> class_atom();
	enum class Braces { None, Read, Failed };
	Braces braces(uint32_t *r_min, uint32_t *r_max);
	bool quantify(uint32_t *r_node);

public:
	Parser(std::u16string_view p_text, Program &r_program) :
			text_(p_text), program_(r_program) {}
	std::optional<Tree> read(std::string *r_error);
};

std::optional<Tree> Parser::read(std::string *r_error) {
	program_.groups = count_groups(text_);
	program_.names.assign(program_.groups, std::u16string());
	std::optional<uint32_t> root = disjunction();
	if (root && more()) {
		root = std::nullopt;
		fail("a ')' closes no group");
	}
	for (const NameUse &use : name_uses_) {
		const auto named = std::find(program_.names.begin(), program_.names.end(), use.name);
		if (named == program_.names.end()) {
			root = std::nullopt;
			fail("no group is named '" + narrow(use.name) + "'");
			break;
		}
		tree_.nodes[use.node].value = static_cast<uint32_t>(named - program_.names.begin()) + 1;
	}
	if (!root) {
		*r_error = error_;
		return std::nullopt;
	}
	tree_.root = *root;
	return std::move(tree_);
}

std::optional<uint32_t> Parser::disjunction() {
	if (++depth_ > MAX_NESTING) {
		fail("groups are nested too deeply");
		return std::nullopt;
	}
	std::vector<uint32_t> alternatives;
	for (;;) {
		const std::optional<uint32_t> one = alternative();
		if (!one) {
			return std::nullopt;
		}
		alternatives.push_back(*one);
		if (peek() != u'|' || !more()) {
			break;
		}
		at_++;
	}
	depth_--;
	if (alternatives.size() == 1) {
		return alternatives[0];
	}
	Node node;
	node.kind = Node::Alternation;
	node.children = std::move(alternatives);
	return add(std::move(node));
}

std::optional<uint32_t> Parser::alternative() {
	std::vector<uint32_t> terms;
	for (;;) {
		skip_extended();
		if (!more() || peek() == u'|' || peek() == u')') {
			break;
		}
		const uint32_t first = next_group_;
		std::optional<uint32_t> term = atom();
		if (!term) {
			return std::nullopt;
		}
		tree_.nodes[*term].first_group = first;
		tree_.nodes[*term].end_group = next_group_;
		skip_extended();
		if (!quantify(&*term)) {
			return std::nullopt;
		}
		terms.push_back(*term);
	}
	if (terms.size() == 1) {
		return terms[0];
	}
	Node node;
	node.kind = terms.empty() ? Node::Empty : Node::Concat;
	node.children = std::move(terms);
	return add(std::move(node));
}

std::optional<uint32_t> Parser::atom() {
	const char16_t unit = text_[at_];
	switch (unit) {
		case u'^':
		case u'$':
		case u'.':
			at_++;
			return leaf(unit == u'^' ? Node::LineStart : unit == u'$' ? Node::LineEnd : Node::Any);
		case u'(':
			return group();
		case u'[':
			return character_class();
		case u'\\':
			return escape();
		case u'*':
		case u'+':
		case u'?':
			fail("a quantifier with nothing to repeat");
			return std::nullopt;
		case u'{': {
			uint32_t min = 0, max = 0;
			const size_t start = at_;
			if (braces(&min, &max) != Braces::None) {
				fail("a quantifier with nothing to repeat");
				return std::nullopt;
			}
			at_ = start + 1;
			return leaf(Node::Unit, unit);
		}
		default:
			at_++;
			return leaf(Node::Unit, unit);
	}
}

// `{n}`, `{n,}` or `{n,m}` at the brace: read, or not a quantifier at all (the brace is then itself).
Parser::Braces Parser::braces(uint32_t *r_min, uint32_t *r_max) {
	size_t at = at_ + 1;
	const auto number = [&](uint32_t *r_value) {
		const size_t start = at;
		uint64_t value = 0;
		for (; at < text_.size() && is_digit(text_[at]); at++) {
			value = std::min<uint64_t>(value * 10 + (text_[at] - u'0'), uint64_t(MAX_COUNT) + 1);
		}
		*r_value = static_cast<uint32_t>(value);
		return at > start;
	};
	if (!number(r_min)) {
		return Braces::None;
	}
	*r_max = *r_min;
	if (at < text_.size() && text_[at] == u',') {
		at++;
		if (!number(r_max)) {
			*r_max = UNBOUNDED;
		}
	}
	if (at >= text_.size() || text_[at] != u'}') {
		return Braces::None;
	}
	at_ = at + 1;
	if (*r_min > MAX_COUNT || (*r_max != UNBOUNDED && *r_max > MAX_COUNT)) {
		fail("a quantifier's count is too large");
		return Braces::Failed;
	}
	if (*r_max < *r_min) {
		fail("a quantifier's counts are in the wrong order");
		return Braces::Failed;
	}
	return Braces::Read;
}

bool Parser::quantify(uint32_t *r_node) {
	uint32_t min = 0, max = UNBOUNDED;
	const char16_t unit = peek();
	if (!more()) {
		return true;
	}
	if (unit == u'*' || unit == u'+' || unit == u'?') {
		min = unit == u'+' ? 1 : 0;
		max = unit == u'?' ? 1 : UNBOUNDED;
		at_++;
	} else if (unit != u'{') {
		return true;
	} else if (const Braces read = braces(&min, &max); read != Braces::Read) {
		return read == Braces::None;
	}
	const Node &target = tree_.nodes[*r_node];
	const bool assertion = target.kind == Node::LineStart || target.kind == Node::LineEnd || target.kind == Node::Boundary ||
			(target.kind == Node::Look && target.value != static_cast<uint32_t>(LookKind::Atomic));
	if (assertion) {
		return fail("a quantifier with nothing to repeat");
	}
	Node repeat;
	repeat.kind = Node::Repeat;
	repeat.min = min;
	repeat.max = max;
	repeat.flag = peek() != u'?';
	repeat.first_group = target.first_group;
	repeat.end_group = target.end_group;
	repeat.children = { *r_node };
	const bool possessive = peek() == u'+';
	if (peek() == u'?' || possessive) {
		at_++;
	}
	*r_node = add(repeat);
	if (possessive) {
		Node atomic;
		atomic.kind = Node::Look;
		atomic.value = static_cast<uint32_t>(LookKind::Atomic);
		atomic.first_group = repeat.first_group;
		atomic.end_group = repeat.end_group;
		atomic.children = { *r_node };
		*r_node = add(std::move(atomic));
	}
	return true;
}

std::optional<uint32_t> Parser::group() {
	at_++;
	if (peek() == u'?') {
		at_++;
		return special_group();
	}
	Node node;
	node.kind = Node::Group;
	node.value = next_group_++;
	return group_body(std::move(node));
}

// A group that begins `(?`: not capturing, a lookaround, atomic, named, or a reference to a named group.
std::optional<uint32_t> Parser::special_group() {
	Node node;
	node.kind = Node::Look;
	const char16_t kind = peek();
	at_++;
	if (kind == u':') {
		node.kind = Node::Group;
		node.value = NO_GROUP;
	} else if (kind == u'=' || kind == u'!') {
		node.value = static_cast<uint32_t>(kind == u'=' ? LookKind::Ahead : LookKind::NotAhead);
	} else if (kind == u'>') {
		node.value = static_cast<uint32_t>(LookKind::Atomic);
	} else if (kind == u'<' && (peek() == u'=' || peek() == u'!')) {
		node.value = static_cast<uint32_t>(peek() == u'=' ? LookKind::Behind : LookKind::NotBehind);
		at_++;
	} else if (kind == u'P' && peek() == u'=') {
		at_++;
		const std::optional<std::u16string> name = group_name(u')');
		if (!name) {
			return std::nullopt;
		}
		const uint32_t reference = leaf(Node::Backref);
		name_uses_.push_back({ reference, *name });
		return reference;
	} else if (kind == u'<' || (kind == u'P' && peek() == u'<')) {
		at_ += kind == u'P' ? 1 : 0;
		node.kind = Node::Group;
		node.value = next_group_++;
		const std::optional<std::u16string> name = group_name(u'>');
		if (!name) {
			return std::nullopt;
		}
		if (std::find(program_.names.begin(), program_.names.end(), *name) != program_.names.end()) {
			fail("two groups are named '" + narrow(*name) + "'");
			return std::nullopt;
		}
		program_.names[node.value - 1] = *name;
	} else {
		fail("this kind of group is not supported");
		return std::nullopt;
	}
	return group_body(std::move(node));
}

// A group's name, up to p_end, which is read too.
std::optional<std::u16string> Parser::group_name(char16_t p_end) {
	const size_t start = at_;
	while (more() && is_name_unit(peek())) {
		at_++;
	}
	if (at_ == start || peek() != p_end || !more()) {
		fail("a group's name is not closed");
		return std::nullopt;
	}
	at_++;
	return std::u16string(text_.substr(start, at_ - 1 - start));
}

std::optional<uint32_t> Parser::group_body(Node p_node) {
	const std::optional<uint32_t> body = disjunction();
	if (!body) {
		return std::nullopt;
	}
	if (peek() != u')' || !more()) {
		fail("a group is not closed");
		return std::nullopt;
	}
	at_++;
	p_node.children = { *body };
	const bool behind = p_node.kind == Node::Look &&
			(p_node.value == static_cast<uint32_t>(LookKind::Behind) || p_node.value == static_cast<uint32_t>(LookKind::NotBehind));
	if (behind) {
		const std::optional<uint32_t> length = fixed_length(tree_, *body);
		if (!length) {
			fail("a lookbehind must have a fixed length");
			return std::nullopt;
		}
		p_node.min = *length;
	}
	return add(std::move(p_node));
}

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

} // namespace

std::optional<Tree> parse(std::u16string_view p_pattern, Program &r_program, std::string *r_error) {
	return Parser(p_pattern, r_program).read(r_error);
}

} // namespace regex
} // namespace vm
} // namespace kas3
