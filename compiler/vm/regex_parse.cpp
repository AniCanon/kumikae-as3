#include "regex_parser.h"

#include <algorithm>
#include <array>

namespace kas3 {
namespace vm {
namespace regex {

namespace {

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

} // namespace

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

std::optional<Tree> parse(std::u16string_view p_pattern, Program &r_program, std::string *r_error) {
	return Parser(p_pattern, r_program).read(r_error);
}

} // namespace regex
} // namespace vm
} // namespace kas3
