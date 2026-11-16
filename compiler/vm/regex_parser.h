// The reader of a pattern, shared by regex_parse.cpp (the structure: alternatives, groups, quantifiers)
// and regex_parse_escapes.cpp (escapes and classes). Nothing else includes this.
#pragma once

#include "regex_tree.h"

namespace kas3 {
namespace vm {
namespace regex {

using Range = std::pair<char16_t, char16_t>;

inline bool is_digit(char16_t p_unit) {
	return p_unit >= u'0' && p_unit <= u'9';
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

} // namespace regex
} // namespace vm
} // namespace kas3
