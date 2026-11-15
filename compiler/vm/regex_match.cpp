#include "regex.h"

#include "text_units.h"

namespace kas3 {
namespace vm {
namespace regex {

namespace {

constexpr uint32_t REGISTER = 0x80000000u; // In an undo entry: the index is a register's, not a slot's.

bool is_line_break(char16_t p_unit) {
	return p_unit == u'\n' || p_unit == u'\r' || p_unit == 0x2028 || p_unit == 0x2029;
}

bool is_word(char16_t p_unit) {
	return (p_unit >= u'0' && p_unit <= u'9') || (p_unit >= u'A' && p_unit <= u'Z') || (p_unit >= u'a' && p_unit <= u'z') || p_unit == u'_';
}

bool in_ranges(const Set &p_set, char16_t p_unit) {
	for (const auto &[from, to] : p_set.ranges) {
		if (p_unit >= from && p_unit <= to) {
			return true;
		}
	}
	return false;
}

// A class takes a unit if it takes any unit that is the same to a case-insensitive pattern (15.10.2.8).
bool in_set(const Set &p_set, char16_t p_unit, bool p_fold) {
	if (in_ranges(p_set, p_unit)) {
		return true;
	}
	if (!p_fold) {
		return false;
	}
	const char16_t same = canonical(p_unit);
	for (const uint32_t other : { to_lower(p_unit), to_upper(p_unit), static_cast<uint32_t>(same) }) {
		if (other <= 0xFFFF && other != p_unit && canonical(static_cast<char16_t>(other)) == same && in_ranges(p_set, static_cast<char16_t>(other))) {
			return true;
		}
	}
	return false;
}

// Runs a program over a text. Every way the search could still go is kept on one stack, with how to
// undo what was written to a slot or a register since; going back takes the stack down to the last way
// still open. Only a lookaround runs a part of the program on its own, by recursion, so the C++ stack
// grows with how deep they nest in the pattern and never with the text.
class Matcher {
	const Program &program_;
	std::u16string_view text_;
	const Limits &limits_;
	std::vector<int32_t> &slots_;
	std::vector<int32_t> registers_;
	struct Entry {
		bool undo; // Undo a write to `index`, which had `value`; else go on at instruction `index`, unit `value`.
		uint32_t index;
		int32_t value;
	};
	std::vector<Entry> stack_;
	uint64_t steps_ = 0;
	bool limit_ = false;

	int32_t &place(uint32_t p_index) { return p_index & REGISTER ? registers_[p_index & ~REGISTER] : slots_[p_index]; }
	void write(uint32_t p_index, int32_t p_value) {
		stack_.push_back({ true, p_index, place(p_index) });
		place(p_index) = p_value;
	}
	// Takes the stack down to p_base, undoing what was written.
	void unwind(size_t p_base) {
		while (stack_.size() > p_base) {
			const Entry entry = stack_.back();
			stack_.pop_back();
			if (entry.undo) {
				place(entry.index) = entry.value;
			}
		}
	}
	// The last way still open above p_base, undoing what was written since; false if there is none.
	bool back(size_t p_base, uint32_t *r_pc, int32_t *r_at) {
		while (stack_.size() > p_base) {
			const Entry entry = stack_.back();
			stack_.pop_back();
			if (entry.undo) {
				place(entry.index) = entry.value;
			} else {
				*r_pc = entry.index;
				*r_at = entry.value;
				return true;
			}
		}
		return false;
	}
	// The ways a lookaround or atomic group left open are closed; what it wrote stays, and can be undone.
	void close(size_t p_base) {
		size_t kept = p_base;
		for (size_t i = p_base; i < stack_.size(); i++) {
			if (stack_[i].undo) {
				stack_[kept++] = stack_[i];
			}
		}
		stack_.resize(kept);
	}
	bool same_text(int32_t p_from, int32_t p_to, int32_t *r_at, bool p_fold) const;
	bool look(const Instruction &p_in, uint32_t p_pc, int32_t *r_at);
	bool step(const Instruction &p_in, uint32_t *r_pc, int32_t *r_at);

public:
	Matcher(const Program &p_program, std::u16string_view p_text, const Limits &p_limits, std::vector<int32_t> &r_slots) :
			program_(p_program), text_(p_text), limits_(p_limits), slots_(r_slots), registers_(p_program.registers, -1) {}
	bool limited() const { return limit_; }
	// Runs from instruction p_pc at unit p_at to a Match: where the match ends, or -1. A run that fails
	// leaves the stack as it found it.
	int32_t run(uint32_t p_pc, int32_t p_at);
};

int32_t Matcher::run(uint32_t p_pc, int32_t p_at) {
	const size_t base = stack_.size();
	uint32_t pc = p_pc;
	int32_t at = p_at;
	for (;;) {
		if (limit_) {
			unwind(base);
			return -1;
		}
		const Instruction &in = program_.code[pc];
		if (in.code == Code::Match) {
			return at;
		}
		if (++steps_ > limits_.steps || stack_.size() > limits_.backtrack) {
			limit_ = true;
			continue;
		}
		if (!step(in, &pc, &at) && !back(base, &pc, &at)) {
			return -1;
		}
	}
}

// One instruction: true, and where to go on, or false if the search must go back.
bool Matcher::step(const Instruction &p_in, uint32_t *r_pc, int32_t *r_at) {
	const int32_t at = *r_at;
	const bool inside = at < static_cast<int32_t>(text_.size());
	const char16_t unit = inside ? text_[at] : 0;
	const char16_t before = at > 0 ? text_[at - 1] : 0;
	bool taken = false; // A unit matched, and the search goes on after it.
	switch (p_in.code) {
		case Code::Unit:
			taken = inside && (p_in.flag ? canonical(unit) : unit) == p_in.a;
			break;
		case Code::Any:
			taken = inside && (p_in.flag || !is_line_break(unit));
			break;
		case Code::Set:
			taken = inside && in_set(program_.sets[p_in.a], unit, (program_.flags & IGNORE_CASE) != 0) != p_in.flag;
			break;
		case Code::LineStart:
			if (!(at == 0 || (p_in.flag && is_line_break(before)))) {
				return false;
			}
			break;
		case Code::LineEnd:
			if (!(!inside || (p_in.flag && is_line_break(unit)))) {
				return false;
			}
			break;
		case Code::Boundary: {
			const bool edge = (at > 0 && is_word(before)) != (inside && is_word(unit));
			if (edge == p_in.flag) {
				return false;
			}
			break;
		}
		case Code::Split:
			stack_.push_back({ false, p_in.b, at });
			*r_pc = p_in.a;
			return true;
		case Code::Jump:
			*r_pc = p_in.a;
			return true;
		case Code::Save:
			write(p_in.a, at);
			break;
		case Code::Clear:
			for (uint32_t slot = p_in.a; slot < p_in.b; slot++) {
				if (slots_[slot] != -1) {
					write(slot, -1);
				}
			}
			break;
		case Code::Mark:
			write(p_in.a | REGISTER, at);
			break;
		case Code::Progress:
			if (registers_[p_in.a] == at) {
				return false;
			}
			break;
		case Code::Backref: {
			int32_t end = at;
			if (!same_text(slots_[2 * p_in.a], slots_[2 * p_in.a + 1], &end, p_in.flag)) {
				return false;
			}
			*r_at = end;
			break;
		}
		case Code::Look:
			if (!look(p_in, *r_pc, r_at)) {
				return false;
			}
			*r_pc = p_in.a;
			return true;
		case Code::Match:
			return false;
	}
	if (p_in.code == Code::Unit || p_in.code == Code::Any || p_in.code == Code::Set) {
		if (!taken) {
			return false;
		}
		*r_at = at + 1;
	}
	(*r_pc)++;
	return true;
}

// What a group took, again from *r_at; a group that took no part takes nothing (15.10.2.9).
bool Matcher::same_text(int32_t p_from, int32_t p_to, int32_t *r_at, bool p_fold) const {
	if (p_from < 0 || p_to < 0) {
		return true;
	}
	const int32_t length = p_to - p_from;
	if (*r_at + length > static_cast<int32_t>(text_.size())) {
		return false;
	}
	for (int32_t i = 0; i < length; i++) {
		const char16_t a = text_[p_from + i], b = text_[*r_at + i];
		if (a != b && !(p_fold && canonical(a) == canonical(b))) {
			return false;
		}
	}
	*r_at += length;
	return true;
}

// A lookaround takes nothing, and an atomic group takes what it first matched: neither is gone back into.
bool Matcher::look(const Instruction &p_in, uint32_t p_pc, int32_t *r_at) {
	const LookKind kind = static_cast<LookKind>(p_in.b);
	const bool behind = kind == LookKind::Behind || kind == LookKind::NotBehind;
	const bool negated = kind == LookKind::NotAhead || kind == LookKind::NotBehind;
	const size_t base = stack_.size();
	const bool room = !behind || *r_at >= static_cast<int32_t>(p_in.c);
	const int32_t end = room ? run(p_pc + 1, behind ? *r_at - static_cast<int32_t>(p_in.c) : *r_at) : -1;
	if (limit_) {
		return false;
	}
	if (end < 0) {
		return negated;
	}
	if (negated) {
		unwind(base);
		return false;
	}
	close(base);
	if (kind == LookKind::Atomic) {
		*r_at = end;
	}
	return true;
}

} // namespace

char16_t canonical(char16_t p_unit) {
	const uint32_t upper = to_upper(p_unit);
	if (upper > 0xFFFF || (p_unit >= 128 && upper < 128)) {
		return p_unit;
	}
	return static_cast<char16_t>(upper);
}

Outcome search(const Program &p_program, std::u16string_view p_text, uint32_t p_from, std::vector<int32_t> *r_captures, const Limits &p_limits) {
	std::vector<int32_t> &slots = *r_captures;
	Matcher matcher(p_program, p_text, p_limits, slots); // One count of steps for every place a match may start.
	for (uint32_t start = p_from; start <= p_text.size(); start++) {
		slots.assign(2 * (p_program.groups + 1), -1);
		const int32_t end = matcher.run(0, static_cast<int32_t>(start));
		if (matcher.limited()) {
			return Outcome::Limit;
		}
		if (end >= 0) {
			slots[0] = static_cast<int32_t>(start);
			slots[1] = end;
			return Outcome::Found;
		}
	}
	return Outcome::NotFound;
}

} // namespace regex
} // namespace vm
} // namespace kas3
