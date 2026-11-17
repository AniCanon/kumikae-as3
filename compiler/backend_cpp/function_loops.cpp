// A loop over vectors of numbers, or of objects it only reads, written twice.
//
// An element read or written by its index looks at the vector each time: is there one, is the index below
// its length, where are its elements. In a loop that is most of what the loop does, and a write through
// an element makes the C++ compiler read the length and the place again. So a loop in which nothing can
// change a vector but its own writes of elements is written a second time, before the first: there the
// length and the elements of each vector it indexes are locals, set as the loop is entered, and an index
// is held against the local. An index that is not below it leaves for the same instruction of the loop as
// it always was, which does whatever is to be done (a longer vector, an error) and finishes the loop.
// With every vector there and every index in range, only the copy with the locals runs.
//
// Which loops: one closed by a plain jump back, in which every instruction is one that runs no code
// (facts.h), a call is of a function that runs none and stores no element, every element stored is a
// number or a Boolean under an int or uint index, and no register holding a vector it indexes is written.
// An element that is an object is read that way too, into a register that counts no reference (lent_):
// `var b:Body = bodies[i]` in a function that lets go of nothing.
#include "function_writer.h"

#include "names.h"

#include <algorithm>
#include <set>

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

constexpr size_t MOST_VECTORS = 4; // Of one loop, each with two locals.

bool is_whole(Kind p_kind) {
	return p_kind == Kind::I32 || p_kind == Kind::U32;
}

bool is_number(Kind p_kind) {
	return is_whole(p_kind) || p_kind == Kind::F64 || p_kind == Kind::Bool;
}

// Where an instruction jumps to, or NONE.
uint32_t target_of(const Instruction &p_in) {
	return p_in.op == Op::Jump ? p_in.a : p_in.op == Op::JumpIf || p_in.op == Op::JumpIfNot ? p_in.b : NONE;
}

} // namespace

// Whether an element read into the register is a plain copy of what the vector's place holds: a number, a
// Boolean, or an object the register counts no reference to.
bool FunctionWriter::reads_in_place(uint32_t p_register) const {
	return is_number(kind(p_register)) || (kind(p_register) == Kind::Obj && lent_[p_register]);
}

// Whether a register's vector holds ints or uints, by the elements read out of it and stored in it: each
// place is then four bytes, the number itself (runtime/fast.h).
bool FunctionWriter::narrow_vector(uint32_t p_vector) const {
	for (const Instruction &in : function_.code) {
		if ((in.op == Op::VecGet && in.b == p_vector && is_whole(kind(in.a))) || (in.op == Op::VecSet && in.a == p_vector && is_whole(kind(in.c)))) {
			return true;
		}
	}
	return false;
}

// The vector registers the loop indexes, if it is one to write twice: none otherwise.
std::vector<uint32_t> FunctionWriter::loop_vectors(uint32_t p_from, uint32_t p_to) const {
	std::set<uint32_t> vectors;
	for (uint32_t pc = p_from; pc <= p_to; pc++) {
		const Instruction &in = function_.code[pc];
		if (runs_code(in)) {
			return {};
		}
		if (const FunctionFacts *callee = is_call(in.op) ? called(pc) : nullptr; is_call(in.op) && (!callee || callee->opaque || callee->stores_elements)) {
			return {};
		}
		if (in.op == Op::VecSet) {
			if (!is_whole(kind(in.b)) || !is_number(kind(in.c))) {
				return {};
			}
			vectors.insert(in.a);
		} else if (in.op == Op::VecGet && is_whole(kind(in.c)) && reads_in_place(in.a)) {
			vectors.insert(in.b);
		}
	}
	// A register whose elements are read or stored as ints here and as something else there is of no one
	// vector's class: nothing is known of its places.
	for (const Instruction &in : function_.code) {
		const uint32_t vector = in.op == Op::VecGet ? in.b : in.op == Op::VecSet ? in.a : NONE;
		if (vector != NONE && vectors.count(vector) && narrow_vector(vector) != is_whole(kind(in.op == Op::VecGet ? in.a : in.c))) {
			return {};
		}
	}
	for (const ir::Handler &handler : function_.handlers) {
		if (vectors.count(handler.reg)) {
			return {};
		}
	}
	for (uint32_t pc = p_from; pc <= p_to; pc++) {
		const Instruction &in = function_.code[pc];
		const char first = ir::op_shape(in.op)[0];
		// A register that borrows a field of `this` is read from it again each time round: the same vector,
		// as long as nothing in the loop writes that field.
		const bool written = (first == 'R' || first == 'r') && in.a != NONE && !first_is_read(in.op) && vectors.count(in.a);
		if (written && borrowed_[in.a] == NONE) {
			return {};
		}
		for (uint32_t each : vectors) {
			const uint32_t slot = borrowed_[each];
			const bool sets = in.op == Op::SetField && in.b == slot && may_be_this(in.a);
			if (slot != NONE && (sets || (is_call(in.op) && may_be_field(module_, called(pc)->writes, function_.owner, slot)))) {
				return {};
			}
		}
	}
	if (vectors.size() > MOST_VECTORS) {
		return {};
	}
	return std::vector<uint32_t>(vectors.begin(), vectors.end());
}

void FunctionWriter::find_twice() {
	retried_.assign(function_.code.size() + 1, false);
	if (!facts_) {
		return;
	}
	std::vector<std::pair<uint32_t, uint32_t>> loops;
	for (uint32_t pc = 0; pc < function_.code.size(); pc++) {
		if (function_.code[pc].op == Op::Jump && function_.code[pc].a <= pc) {
			loops.emplace_back(function_.code[pc].a, pc);
		}
	}
	// The widest first: a loop inside one that is written twice is in both of its copies already.
	std::sort(loops.begin(), loops.end(), [](const auto &p_a, const auto &p_b) { return p_a.second - p_a.first > p_b.second - p_b.first; });
	for (const std::pair<uint32_t, uint32_t> &loop : loops) {
		const uint32_t from = loop.first;
		const uint32_t to = loop.second;
		const bool apart = std::all_of(twice_.begin(), twice_.end(), [&](const Twice &p_each) { return to < p_each.from || from > p_each.to; });
		if (!apart) {
			continue;
		}
		std::vector<uint32_t> vectors = loop_vectors(from, to);
		if (!vectors.empty()) {
			twice_.push_back(Twice{ from, to, std::move(vectors) });
		}
	}
}

std::string FunctionWriter::label(uint32_t p_target) const {
	const bool fast = fast_ && p_target >= fast_->from && p_target <= fast_->to;
	return (fast ? "V" : "L") + std::to_string(p_target);
}

std::string FunctionWriter::twice_declared() const {
	std::set<uint32_t> vectors;
	for (const Twice &each : twice_) {
		vectors.insert(each.vectors.begin(), each.vectors.end());
	}
	std::string out;
	for (uint32_t each : vectors) {
		const std::string number = std::to_string(each);
		out += std::string("\t") + (narrow_vector(each) ? "int32_t" : "Slot") + " *d" + number + " = nullptr;\n\tuint32_t n" + number + " = 0;\n\t(void)d" + number +
				"; (void)n" + number + ";\n";
	}
	return out;
}

// An element or the length of a vector whose length and elements are locals here.
bool FunctionWriter::fast_vector(const Instruction &p_in) {
	const auto local = [&](uint32_t p_register) { return std::find(fast_->vectors.begin(), fast_->vectors.end(), p_register) != fast_->vectors.end(); };
	const auto element = [&](uint32_t p_vector, uint32_t p_index) {
		const std::string index = "static_cast<uint32_t>(" + reg(p_index) + ")";
		line("if (KAS3_UNLIKELY(" + index + " >= n" + std::to_string(p_vector) + ")) goto S" + std::to_string(pc_) + ";");
		retried_[pc_] = true;
		return "d" + std::to_string(p_vector) + "[" + index + "]";
	};
	switch (p_in.op) {
		case Op::VecLength:
			if (!local(p_in.b)) {
				return false;
			}
			line(reg(p_in.a) + " = static_cast<" + std::string(type_of(kind(p_in.a))) + ">(n" + std::to_string(p_in.b) + ");");
			return true;
		case Op::VecGet:
			if (!local(p_in.b) || !is_whole(kind(p_in.c)) || !reads_in_place(p_in.a)) {
				return false;
			}
			// A place of ints or uints is the number; any other is a slot.
			line(reg(p_in.a) + " = " + (narrow_vector(p_in.b) ? "static_cast<" + std::string(type_of(kind(p_in.a))) + ">(" + element(p_in.b, p_in.c) + ")"
															  : from_slot(kind(p_in.a), element(p_in.b, p_in.c))) + ";");
			return true;
		case Op::VecSet:
			if (!local(p_in.a)) {
				return false;
			}
			line(element(p_in.a, p_in.b) + " = " + (narrow_vector(p_in.a) ? "static_cast<int32_t>(" + reg(p_in.c) + ")" : "slot(" + reg(p_in.c) + ")") + ";");
			return true;
		default:
			return false;
	}
}

// The loop's copy with the locals: entered only with every vector there, and left by its own jumps or
// for the other copy.
std::string FunctionWriter::fast_copy(const Twice &p_loop, const std::vector<bool> &p_landed) {
	std::vector<bool> inside(function_.code.size() + 1, false); // What a jump of the loop itself goes to.
	for (uint32_t pc = p_loop.from; pc <= p_loop.to; pc++) {
		const uint32_t target = target_of(function_.code[pc]);
		if (target != NONE && target >= p_loop.from && target <= p_loop.to) {
			inside[target] = true;
		}
	}
	std::string missing;
	std::string places;
	std::string lengths;
	std::vector<std::pair<std::string, std::string>> found; // What holds a vector, and the number of the first register it was read for.
	for (uint32_t each : p_loop.vectors) {
		const std::string number = std::to_string(each);
		// A register that borrows a field may not have been read into yet: the field itself is what it will hold.
		const std::string held = (borrowed_[each] != NONE ? own_field(borrowed_[each]) : register_name(each)) + (narrow_vector(each) ? "/n" : "/s");
		// Two registers read from one field hold one vector: the second's places are the first's, said so
		// that the C++ compiler knows them for the same memory (`y[i] = a * x[i] + y[i]` reads `y` twice).
		const auto same = std::find_if(found.begin(), found.end(), [&](const auto &p_each) { return p_each.first == held; });
		if (same != found.end()) {
			places += "\td" + number + " = d" + same->second + ";\n";
			lengths += "\tn" + number + " = n" + same->second + ";\n";
			continue;
		}
		found.emplace_back(held, number);
		const std::string vector = held.substr(0, held.size() - 2);
		// No places: no vector, one with no room yet, or one whose places are not what the loop reads there.
		places += "\td" + number + " = " + (narrow_vector(each) ? "numbers_in(" : "slots_in(") + vector + ");\n";
		missing += (missing.empty() ? "!d" : " || !d") + number;
		lengths += "\tn" + number + " = static_cast<Vector *>(" + vector + ")->length;\n";
	}
	std::string out = places + "\tif (KAS3_UNLIKELY(" + missing + ")) goto L" + std::to_string(p_loop.from) + ";\n" + lengths;
	const uint32_t resume = pc_;
	fast_ = &p_loop;
	for (pc_ = p_loop.from; pc_ <= p_loop.to; pc_++) {
		body_.clear();
		if (p_landed[pc_]) {
			whole_.assign(function_.registers.size(), Whole());
			text_.assign(function_.registers.size(), NONE);
		}
		instruction(function_.code[pc_]);
		out += (inside[pc_] ? "V" + std::to_string(pc_) + ":;\n" : std::string()) + body_;
	}
	fast_ = nullptr;
	pc_ = resume;
	return out;
}

} // namespace backend_cpp
} // namespace kas3
