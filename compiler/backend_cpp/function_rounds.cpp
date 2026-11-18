// Where a loop goes round, and where a loop without end is stopped: Flash's Error #1502, as the interpreter
// raises it for a call that runs too long. The host has a watch on how long the call has run, which sets an
// alarm (runtime/time_limit.h); generated code looks at the alarm once in ROUNDS times round a loop, in one
// of two ways.
//
// A function counts its rounds in a local (`rounds`), taken down at each jump back. The jump that is
// counted goes to the loop's own label, and the one after a look goes to a label before it (`R`), so that
// the C++ compiler sees two loops, the inner of which goes round a known number of times: it still does
// several rounds at once where it could before.
//
// A loop counted by one whole-number register against a bound that stays as it is (`for (i = 0; i < n;
// i++)`) pays nothing each round. Its test is held against the end of a stretch (`e`) worked out in one
// place, where the loop is entered and where a stretch is used up: no further than ROUNDS steps on, than
// the loop's bound, and than where one more step would wrap. The counter short of it means the loop's own
// test holds, with one comparison as before. At the stretch's end the loop's own test is asked: it ends
// the loop, or the alarm is looked at and the next stretch begins. With a step that is nothing or goes
// the other way there is no stretch, and the look is made each round.
//
// Where the loop's vectors are locals and the counter is their index, the stretch's end is held as an
// index too (`x`), and is no further on than any of those vectors is long: an index short of it is one of
// each vector's, so nothing more is asked where an element is read or stored, and the C++ compiler does
// several rounds at once. A counter that is no index (below 0, or past a vector's end) leaves for the
// loop as written.
//
// Which loops are counted that way: one closed by a single jump back, that begins with nothing but its
// test (a `<` or `<=` between the counter and the bound, either way round, worked out by what runs no
// code and can be worked out again, and the jump out), whose
// counter is written once in it, by adding to it or taking from it a step that stays as it is, on every
// way round. The bound and the step are sums of constants, of registers the loop does not write, of
// static fields the function keeps in locals and, where the loop's vectors are locals, of their lengths.
#include "function_writer.h"

#include "names.h"

#include <algorithm>
#include <cmath>

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

constexpr uint32_t MOST_PARTS = 6; // Of a bound or a step: sums nest no deeper.

bool is_whole(Kind p_kind) {
	return p_kind == Kind::I32 || p_kind == Kind::U32;
}

} // namespace

std::vector<uint32_t> FunctionWriter::writes_in(uint32_t p_register, uint32_t p_from, uint32_t p_to) const {
	std::vector<uint32_t> found;
	for (uint32_t pc = p_from; pc <= p_to; pc++) {
		const Instruction &in = function_.code[pc];
		const char first = ir::op_shape(in.op)[0];
		const bool writes = (first == 'R' || first == 'r') && in.a == p_register && !first_is_read(in.op);
		if (writes || changed_in_place(function_, in) == p_register) {
			found.push_back(pc);
		}
	}
	return found;
}

// The one instruction of the loop that writes the register, if it is in [p_low, p_high): or NONE.
uint32_t FunctionWriter::written_once(const Counted &p_loop, uint32_t p_register, uint32_t p_low, uint32_t p_high) const {
	const std::vector<uint32_t> writes = writes_in(p_register, p_loop.from, p_loop.to);
	return writes.size() == 1 && writes[0] >= p_low && writes[0] < p_high ? writes[0] : NONE;
}

void FunctionWriter::find_counted() {
	for (uint32_t to = 0; to < function_.code.size(); to++) {
		if (function_.code[to].op != Op::Jump || function_.code[to].a > to) {
			continue;
		}
		Counted loop;
		loop.from = function_.code[to].a;
		loop.to = to;
		if (counted_shape(&loop) && counted_step(&loop)) {
			counted_.push_back(loop);
		}
	}
}

// The loop's test, and that nothing goes round it or into it but by its one jump back.
bool FunctionWriter::counted_shape(Counted *r_loop) const {
	const uint32_t from = r_loop->from;
	const uint32_t to = r_loop->to;
	uint32_t test = NONE;
	for (uint32_t pc = from; pc < to && test == NONE; pc++) {
		const Instruction &in = function_.code[pc];
		test = in.op == Op::LtF64 || in.op == Op::LeF64 ? pc : NONE;
		// Before the test, only what gives a register a number, and can be done again as a stretch begins.
		const bool again = in.op == Op::Stmt || ((in.op == Op::ConstF64 || in.op == Op::ConstI32 || in.op == Op::ConstU32 || in.op == Op::I32ToF64 ||
														 in.op == Op::U32ToF64 || in.op == Op::AddF64 || in.op == Op::SubF64 || in.op == Op::Move ||
														 in.op == Op::GetGlobal || in.op == Op::VecLength) &&
														!ir::is_held(kind(in.a)) && !ir::is_engine(kind(in.a)));
		if (test == NONE && !again) {
			return false;
		}
	}
	if (test == NONE || test + 1 >= to) {
		return false;
	}
	const Instruction &compare = function_.code[test];
	const Instruction &out = function_.code[test + 1];
	if (out.op != Op::JumpIfNot || out.a != compare.a || out.b <= to) {
		return false;
	}
	for (uint32_t pc = 0; pc < function_.code.size(); pc++) {
		const uint32_t target = jump_target(function_.code[pc]);
		const bool inside = pc >= from && pc <= to;
		// Its own jump back, a jump to its start from before it, and what does not land in it at all.
		if (target == NONE || pc == to || (target == from && pc < from) || target < from || target > to) {
			continue;
		}
		// A second jump back to its start (from inside it or from past it), one into it from elsewhere,
		// or one onto its test.
		if (!inside || target <= test + 1) {
			return false;
		}
	}
	for (const ir::Handler &handler : function_.handlers) {
		if (handler.target >= from && handler.target <= to) {
			return false; // An error caught in the loop goes on from where no count is kept.
		}
	}
	r_loop->test = test;
	r_loop->inclusive = compare.op == Op::LeF64;
	// The counter is the side that is a whole-number register made a Number there; the other is the bound.
	for (const bool up : { true, false }) {
		const uint32_t side = up ? compare.b : compare.c;
		const uint32_t made = written_once(*r_loop, side, from, test);
		const Instruction &conversion = function_.code[made == NONE ? test : made];
		if (made != NONE && (conversion.op == Op::I32ToF64 || conversion.op == Op::U32ToF64) && is_whole(kind(conversion.b)) &&
				writes_in(conversion.b, from, to).size() == 1) {
			r_loop->up = up;
			r_loop->counter = conversion.b;
			r_loop->bound = up ? compare.c : compare.b;
			return true;
		}
	}
	return false;
}

// The counter's one write: the step added or taken, on every way from the test to the jump back.
bool FunctionWriter::counted_step(Counted *r_loop) const {
	const uint32_t from = r_loop->from;
	const uint32_t to = r_loop->to;
	const uint32_t body = r_loop->test + 2;
	const uint32_t moved = writes_in(r_loop->counter, from, to)[0];
	const Instruction &move = function_.code[moved];
	if (move.op != Op::Move || moved < body) {
		return false;
	}
	const uint32_t cut = written_once(*r_loop, move.b, body, moved);
	const Instruction &conversion = function_.code[cut == NONE ? moved : cut];
	const Op wanted = kind(r_loop->counter) == Kind::I32 ? Op::F64ToI32 : Op::F64ToU32;
	const uint32_t summed = cut != NONE && conversion.op == wanted ? written_once(*r_loop, conversion.b, body, cut) : NONE;
	if (summed == NONE) {
		return false;
	}
	const Instruction &sum = function_.code[summed];
	if (sum.op != (r_loop->up ? Op::AddF64 : Op::SubF64)) {
		return false;
	}
	// Which side of the sum is the counter, made a Number: for a difference, the left.
	uint32_t first = NONE;
	for (const bool left : { true, false }) {
		const uint32_t side = left ? sum.b : sum.c;
		const uint32_t made = left || r_loop->up ? written_once(*r_loop, side, body, summed) : NONE;
		const Instruction &read = function_.code[made == NONE ? summed : made];
		if (first == NONE && made != NONE && (read.op == Op::I32ToF64 || read.op == Op::U32ToF64) && read.b == r_loop->counter) {
			first = made;
			r_loop->step = left ? sum.c : sum.b;
		}
	}
	if (first == NONE) {
		return false;
	}
	r_loop->stepped = summed;
	r_loop->moved = moved;
	first = std::min(first, parts_begin(*r_loop, r_loop->step, body, summed, MOST_PARTS));
	for (uint32_t pc = 0; pc < function_.code.size(); pc++) {
		const uint32_t target = jump_target(function_.code[pc]);
		if (target == NONE || target < from || target > to) {
			continue;
		}
		// Nothing lands between where the sum's parts are worked out and the counter's write, and nothing
		// before the write goes past it.
		if ((target > first && target <= moved) || (pc >= body && pc < moved && target > moved)) {
			return false;
		}
	}
	return true;
}

// The first instruction of those that work out a Number register in the loop, as still_whole() follows them.
uint32_t FunctionWriter::parts_begin(const Counted &p_loop, uint32_t p_register, uint32_t p_low, uint32_t p_before, uint32_t p_parts) const {
	const uint32_t made = written_once(p_loop, p_register, p_low, p_before);
	if (made == NONE || p_parts == 0) {
		return p_before;
	}
	const Instruction &in = function_.code[made];
	uint32_t first = made;
	if (in.op == Op::AddF64 || in.op == Op::SubF64 || in.op == Op::Move || in.op == Op::I32ToF64 || in.op == Op::U32ToF64) {
		first = std::min(first, parts_begin(p_loop, in.b, p_low, made, p_parts - 1));
	}
	if (in.op == Op::AddF64 || in.op == Op::SubF64) {
		first = std::min(first, parts_begin(p_loop, in.c, p_low, made, p_parts - 1));
	}
	return first;
}

// A Number register of the loop as the whole number it always is while the loop runs, in 64 bits: a sum of
// constants and of what the loop leaves as it is. Nothing where it is anything else. Each part is worked
// out before p_before, in the loop.
std::optional<std::string> FunctionWriter::still_whole(const Counted &p_loop, uint32_t p_register, uint32_t p_low, uint32_t p_before, uint32_t p_parts) const {
	const uint32_t made = written_once(p_loop, p_register, p_low, p_before);
	if (made == NONE || p_parts == 0) {
		return std::nullopt;
	}
	const Instruction &in = function_.code[made];
	switch (in.op) {
		case Op::ConstF64: {
			const double value = module_.numbers[in.b];
			const bool whole = value == std::trunc(value) && std::fabs(value) <= 4294967295.0;
			return whole ? std::optional<std::string>("INT64_C(" + std::to_string(static_cast<int64_t>(value)) + ")") : std::nullopt;
		}
		case Op::Move:
			return kind(in.b) == Kind::F64 ? still_whole(p_loop, in.b, p_low, made, p_parts - 1) : std::nullopt;
		case Op::AddF64:
		case Op::SubF64: {
			const std::optional<std::string> left = still_whole(p_loop, in.b, p_low, made, p_parts - 1);
			const std::optional<std::string> right = left ? still_whole(p_loop, in.c, p_low, made, p_parts - 1) : std::nullopt;
			return right ? std::optional<std::string>("(" + *left + (in.op == Op::AddF64 ? " + " : " - ") + *right + ")") : std::nullopt;
		}
		case Op::I32ToF64:
		case Op::U32ToF64:
			break;
		default:
			return std::nullopt;
	}
	// A whole-number register: one the loop does not write, or one it reads each time round from what stays.
	const std::string type = std::string(type_of(kind(in.b)));
	const std::vector<uint32_t> writes = writes_in(in.b, p_loop.from, p_loop.to);
	if (writes.empty()) {
		return "static_cast<int64_t>(" + register_name(in.b) + ")";
	}
	const Instruction &read = function_.code[writes[0]];
	if (writes.size() != 1 || writes[0] < p_low || writes[0] >= made) {
		return std::nullopt;
	}
	if (read.op == Op::GetGlobal && std::binary_search(still_.begin(), still_.end(), read.b)) {
		return "static_cast<int64_t>(static_cast<" + type + ">(g" + std::to_string(read.b) + "))";
	}
	const bool local = read.op == Op::VecLength && fast_ && std::find(fast_->vectors.begin(), fast_->vectors.end(), read.b) != fast_->vectors.end();
	return local ? std::optional<std::string>("static_cast<int64_t>(static_cast<" + type + ">(n" + std::to_string(read.b) + "))") : std::nullopt;
}

// The loop that begins at the instruction, with what it is counted against in the copy being written: its
// bound and its step. Nothing where the loop is not one, or either is not known to stay as it is there.
auto FunctionWriter::counts(uint32_t p_from) const -> std::optional<Counts> {
	for (const Counted &loop : counted_) {
		if (loop.from != p_from) {
			continue;
		}
		std::optional<std::string> bound = still_whole(loop, loop.bound, loop.from, loop.test, MOST_PARTS);
		std::optional<std::string> step = bound ? still_whole(loop, loop.step, loop.test + 2, loop.stepped, MOST_PARTS) : std::nullopt;
		if (!step) {
			return std::nullopt;
		}
		const std::string counter = "static_cast<int64_t>(" + register_name(loop.counter) + ")";
		const std::string number = std::to_string(loop.from);
		const bool wide = kind(loop.counter) == Kind::U32;
		Counts out;
		out.loop = &loop;
		// A counter that is the index of a vector whose places are locals is held to its stretch as the
		// index it is.
		const std::vector<uint32_t> vectors = loop.up ? indexed(loop) : std::vector<uint32_t>();
		out.index = !vectors.empty();
		out.start = (fast_ ? "Y" : "B") + number;
		out.label = (fast_ ? "W" : "N") + number;
		out.end = (out.index ? "x" : "e") + number;
		out.within = out.index ? "static_cast<uint32_t>(" + register_name(loop.counter) + ") < " + out.end : counter + (loop.up ? " < " : " > ") + out.end;
		const std::string end = loop.inclusive ? "(" + *bound + (loop.up ? " + 1)" : " - 1)") : *bound;
		out.left = loop.up ? end + " - " + counter : counter + " - " + end;
		// The stretch ends before the counter's own type does, so that no step in it wraps.
		const std::string edge = loop.up ? (wide ? "INT64_C(4294967295)" : "INT64_C(2147483647)") : (wide ? "INT64_C(0)" : "INT64_C(-2147483648)");
		out.begin = out.end + " = " + (out.index ? "stretch_index(" : loop.up ? "stretch_up(" : "stretch_down(") + counter + ", " + end + ", " + *step + ", " + edge + ");";
		out.none = out.index ? "0" : loop.up ? "NO_STRETCH_UP" : "NO_STRETCH_DOWN";
		// An int that counts up to an int: its end is no further than an int goes, so less is worked out
		// as a stretch begins, which a loop that goes round a few times does as often as it is entered.
		const bool near = loop.up && !loop.inclusive && !wide && int_bound(loop);
		if (near && out.index) {
			out.begin = out.end + " = stretch_index_near(" + counter + ", " + end + ", " + *step + ");";
			out.leaves = register_name(loop.counter) + " < 0 || " + *step + " <= 0";
		} else if (near && *step == "INT64_C(1)") {
			out.begin = out.end + " = stretch_near(" + counter + ", " + end + ");";
			out.none.clear();
		} else if (out.index) {
			out.leaves = out.end + " == 0";
		}
		// A stretch of indexes ends no further on than each vector does: an index short of it is one of
		// the vector's, with nothing more asked where an element is read or stored (index_held). An index
		// that is not leaves for the loop as written.
		for (const uint32_t each : vectors) {
			const std::string length = "n" + std::to_string(each);
			out.begin += " " + out.end + " = " + out.end + " < " + length + " ? " + out.end + " : " + length + ";";
			out.leaves += " || static_cast<uint32_t>(" + register_name(loop.counter) + ") >= " + length;
		}
		return out;
	}
	return std::nullopt;
}

// The loop's vectors whose elements it reads or stores under its counter between its test and the
// counter's write, where the vectors' places are locals: the counter is there what the test left it. None
// where something jumps back over the write to before it. Sorted.
std::vector<uint32_t> FunctionWriter::indexed(const Counted &p_loop) const {
	std::vector<uint32_t> found;
	if (!fast_) {
		return found;
	}
	for (uint32_t pc = p_loop.moved; pc <= p_loop.to; pc++) {
		if (const uint32_t target = jump_target(function_.code[pc]); target != NONE && target > p_loop.from && target < p_loop.moved) {
			return found;
		}
	}
	const auto local = [&](uint32_t p_vector) { return std::find(fast_->vectors.begin(), fast_->vectors.end(), p_vector) != fast_->vectors.end(); };
	for (uint32_t pc = p_loop.test + 2; pc < p_loop.moved; pc++) {
		const Instruction &in = function_.code[pc];
		const uint32_t vector = in.op == Op::VecGet && in.c == p_loop.counter ? in.b : in.op == Op::VecSet && in.b == p_loop.counter ? in.a : NONE;
		if (vector != NONE && local(vector)) {
			found.push_back(vector);
		}
	}
	std::sort(found.begin(), found.end());
	found.erase(std::unique(found.begin(), found.end()), found.end());
	return found;
}

// Whether the element the instruction being written reads or stores is known to be one of the vector's:
// its index is the counter of a loop held to stretches that end no further on than the vector does.
bool FunctionWriter::index_held(uint32_t p_vector, uint32_t p_index) const {
	for (const Counted &loop : counted_) {
		if (loop.counter != p_index || pc_ <= loop.test + 1 || pc_ >= loop.moved) {
			continue;
		}
		const std::optional<Counts> how = counts(loop.from);
		const std::vector<uint32_t> held = how && how->index ? indexed(loop) : std::vector<uint32_t>();
		return std::find(held.begin(), held.end(), p_vector) != held.end();
	}
	return false;
}

// Whether the loop's bound is one int and nothing more: a register or a static field that holds an int, or
// an int written out.
bool FunctionWriter::int_bound(const Counted &p_loop) const {
	const uint32_t made = written_once(p_loop, p_loop.bound, p_loop.from, p_loop.test);
	if (made == NONE) {
		return false;
	}
	const Instruction &in = function_.code[made];
	if (in.op == Op::ConstF64) {
		const double value = module_.numbers[in.b];
		return value == std::trunc(value) && std::fabs(value) <= 2147483647.0;
	}
	return in.op == Op::I32ToF64 && kind(in.b) == Kind::I32;
}

std::string FunctionWriter::counted_declared() const {
	std::string out;
	for (const Counted &loop : counted_) {
		const std::string number = std::to_string(loop.from);
		out += "\tint64_t e" + number + " = 0;\n\tuint32_t x" + number + " = 0;\n\t(void)e" + number + "; (void)x" + number + ";\n";
	}
	return out;
}

// A counted loop's test, its way out and its start, written for the stretch.
bool FunctionWriter::counted(const Instruction &p_in) {
	for (const Counted &loop : counted_) {
		if (pc_ != loop.test && pc_ != loop.test + 1) {
			continue;
		}
		const std::optional<Counts> how = counts(loop.from);
		if (!how) {
			return false;
		}
		if (pc_ == loop.test) {
			line(reg(p_in.a) + " = " + how->within + ";");
			return true;
		}
		// Out of the stretch: the loop's own test ends the loop, or the alarm is looked at and the next
		// stretch begins where every one does. Where there is none, the round goes on with the look made: or,
		// for a counter that is no index, as the loop as written.
		const std::string looks = "if (alarmed(c) && overdue(c)) { " + fail() + " }";
		line("if (!" + reg(p_in.a) + ") {");
		line("if (" + how->left + " <= 0) goto " + label(p_in.b) + ";");
		if (how->index) {
			line("if (" + how->leaves + ") goto L" + std::to_string(loop.from) + ";");
			line(looks + " goto " + how->start + ";");
		} else if (how->none.empty()) {
			line(looks + " goto " + how->start + ";"); // There is always a stretch from here.
		} else {
			line("if (" + how->end + " != " + how->none + ") { " + looks + " goto " + how->start + "; }");
			line(reg(p_in.a) + " = true;");
			line(looks);
		}
		line("}");
		return true;
	}
	return false;
}

// A jump: one that goes back counts the round, or goes to a counted loop's next round.
std::string FunctionWriter::jump(uint32_t p_target) {
	const std::string to = label(p_target);
	if (p_target > pc_) {
		return "goto " + to + ";";
	}
	if (const std::optional<Counts> how = function_.code[pc_].op == Op::Jump ? counts(p_target) : std::nullopt; how && how->loop->to == pc_) {
		return "goto " + how->label + ";";
	}
	return "if (KAS3_LIKELY(--rounds)) goto " + to + "; rounds = ROUNDS; if (alarmed(c) && overdue(c)) { " + fail() + " } goto R" + to + ";";
}

// Whether a jump back to the instruction is one that is counted in the function's local: its label has
// the one for the look before it.
bool FunctionWriter::rounds_counted(uint32_t p_pc) const {
	if (!rounds_[p_pc]) {
		return false;
	}
	const std::optional<Counts> how = counts(p_pc);
	// The loop's one jump back is the counted loop's own; any other to the same place there is none.
	return !how;
}

} // namespace backend_cpp
} // namespace kas3
