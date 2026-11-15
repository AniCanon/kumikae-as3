// A regular expression as read, before it becomes a program: regex_parse.cpp writes it, regex_compile.cpp
// reads it. Nothing else includes this.
#pragma once

#include "regex.h"

namespace kas3 {
namespace vm {
namespace regex {

constexpr uint32_t UNBOUNDED = 0xFFFFFFFFu; // A quantifier's most, for `*`, `+` and `{n,}`.
constexpr uint32_t NO_GROUP = 0xFFFFFFFFu; // A group that captures nothing: `(?:...)`.

struct Node {
	enum Kind : uint8_t { Empty, Unit, Any, Set, LineStart, LineEnd, Boundary, Group, Look, Backref, Concat, Alternation, Repeat };
	Kind kind = Empty;
	bool flag = false; // Set: negated; Boundary: `\B`; Repeat: greedy.
	uint32_t value = 0; // Unit: the unit; Set: its index; Group: its number or NO_GROUP; Look: a LookKind; Backref: the group.
	uint32_t min = 0; // Repeat: the counts; Look: a lookbehind's length.
	uint32_t max = 0;
	// The groups that begin inside it, by number, from first up to end: a Repeat unsets them as each turn begins.
	uint32_t first_group = 0;
	uint32_t end_group = 0;
	std::vector<uint32_t> children; // By index in the tree.
};

struct Tree {
	std::vector<Node> nodes;
	uint32_t root = 0;
};

// Reads the pattern. The program is given its flags, groups, names and sets here; its code is written
// from the tree after. Nothing, and why in r_error, if the pattern is not one this engine takes.
std::optional<Tree> parse(std::u16string_view p_pattern, Program &r_program, std::string *r_error);

// The ranges of a class escape (\d, \w, \s), or of what they leave out (\D, \W, \S): regex_classes.cpp.
std::vector<std::pair<char16_t, char16_t>> class_ranges(char16_t p_letter);

// Whether the node can match without taking a unit, and how many units it always takes, if it always
// takes the same number.
bool can_be_empty(const Tree &p_tree, uint32_t p_node);
std::optional<uint32_t> fixed_length(const Tree &p_tree, uint32_t p_node);

} // namespace regex
} // namespace vm
} // namespace kas3
