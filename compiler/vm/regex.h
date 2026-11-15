// AS3's regular expressions: a pattern read into a small program, and a backtracking matcher that runs it
// over UTF-16 units, as AS3 indexes strings. Nothing here knows the interpreter.
//
// The syntax and its meaning are ECMAScript 3's (15.10), with what AS3 adds from PCRE: the flags `s`
// (dotall: `.` takes a line break too) and `x` (extended: white space and `#` comments in the pattern are
// left out), named groups `(?P<name>...)` with `(?P=name)` to refer back, lookbehind of a fixed length
// `(?<=...)` `(?<!...)`, atomic groups `(?>...)` and possessive quantifiers `a*+`. A `{` that does not
// begin a quantifier is itself, as in PCRE.
//
// The matcher never recurses for what the text holds, only for how deep lookarounds nest in the pattern,
// and it counts its steps: a pattern that backtracks without end stops at the limit with an outcome that
// says so, never hangs.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kas3 {
namespace vm {
namespace regex {

constexpr uint8_t GLOBAL = 1, IGNORE_CASE = 2, MULTILINE = 4, DOTALL = 8, EXTENDED = 16;
// The flags a RegExp's second argument writes, by letter: g, i, m, s and x. Any other letter is left out.
uint8_t read_flags(std::string_view p_text);
// The flags as RegExp's toString() ends with them, in the order g, i, m, s, x.
std::string flags_text(uint8_t p_flags);

// The most groups, nested, the most instructions a pattern may need, and the largest count a quantifier
// may write: more is refused when the pattern is read (`a{1000}{1000}`).
constexpr uint32_t MAX_NESTING = 256;
constexpr uint32_t MAX_CODE = 1u << 18;
constexpr uint32_t MAX_COUNT = 65535;
// A search takes at most this many steps, plus STEPS_PER_UNIT for each unit of the text, and keeps at
// most MAX_BACKTRACK places to go back to.
constexpr uint64_t STEPS = 10000000;
constexpr uint64_t STEPS_PER_UNIT = 1000;
constexpr size_t MAX_BACKTRACK = size_t(1) << 23;

// ECMAScript's Canonicalize (15.10.2.8): how a case-insensitive pattern compares a unit. Its capital, if
// that is one unit, unless that would take a unit past ASCII into it.
char16_t canonical(char16_t p_unit);

enum class Code : uint8_t {
	Unit, // a: the unit; flag: compared as case-insensitive ECMAScript compares it (a is then folded)
	Any, // `.`; flag: dotall, a line break too
	Set, // a: the set; flag: negated
	LineStart, // `^`; flag: multiline, after a line break too
	LineEnd, // `$`; flag: multiline, before a line break too
	Boundary, // `\b`; flag: `\B`
	Split, // Go on at a; if that fails, at b
	Jump, // a
	Save, // a: the slot of a group's start or end
	Clear, // Slots a up to b are unset: the groups of a quantified atom, as each turn begins
	Backref, // a: the group; flag: case-insensitive
	Mark, // a: the register that keeps where a turn of a loop began
	Progress, // Fails if the turn that register a marks took nothing
	Look, // A lookaround or atomic group, from the next instruction to its Match: a is the instruction after
		  // it, b its LookKind, c the length for a lookbehind
	Match,
};
enum class LookKind : uint8_t { Ahead, NotAhead, Behind, NotBehind, Atomic };

struct Instruction {
	Code code = Code::Match;
	bool flag = false;
	uint32_t a = 0;
	uint32_t b = 0;
	uint32_t c = 0;
};

// The units a class takes, as ranges from and to, both taken in.
struct Set {
	std::vector<std::pair<char16_t, char16_t>> ranges;
};

struct Program {
	std::vector<Instruction> code;
	std::vector<Set> sets;
	uint32_t groups = 0; // The capturing groups; the whole match, group 0, is not counted.
	uint32_t registers = 0; // For Mark and Progress.
	std::vector<std::u16string> names; // For each group from 1: its name, or empty.
	uint8_t flags = 0;
};

// The pattern read; nothing, and why in r_error, if it is not a pattern this engine takes.
std::optional<Program> compile(std::u16string_view p_pattern, uint8_t p_flags, std::string *r_error);

enum class Outcome : uint8_t { Found, NotFound, Limit };

// How far a search may go: its steps, and the places to go back to it keeps.
struct Limits {
	uint64_t steps = STEPS;
	size_t backtrack = MAX_BACKTRACK;
};
// What a search of a text of this length may take.
inline Limits limits_for(size_t p_length) {
	return { STEPS + STEPS_PER_UNIT * p_length, MAX_BACKTRACK };
}

// The first match that starts at or after unit p_from. r_captures gets two entries for each group, the
// whole match first: where it starts and where it ends, or -1 for a group that took no part. Limit: the
// search went past one of p_limits.
Outcome search(const Program &p_program, std::u16string_view p_text, uint32_t p_from, std::vector<int32_t> *r_captures, const Limits &p_limits);

} // namespace regex
} // namespace vm
} // namespace kas3
