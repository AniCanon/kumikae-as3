// A field of `this` that holds an object, read into a register only to be used at once; and an element of
// a vector read into a variable the same way.
//
// `cells[i]`, where cells is a field, reads the field into a register and then the element. The register
// held a reference of its own for that: one counted and one given back each time, in the middle of a loop,
// and a register being let go of is something the other rules (a field in a local, a loop written twice)
// must stop for. But the field itself holds the object for as long as nothing writes the field, so a
// register that is only ever read from one such field, and only used in straight code right after with
// nothing between that could write the field or let go of an object that may end others, borrows: it counts
// nothing and lets go of nothing. What uses it must itself be of that kind: an element read or written, a
// length, a comparison, a return, a call of a function that runs no code not looked into (it stores no
// object and lets go of none: facts.h), and the store of an object into a place of the vector, where
// what the place held ends alone (it is let go of after the vector has been used, with nothing run).
//
// An element read into a variable (`var p:Particle = slots[i]`) is lent the same way: the vector holds the
// object until something takes it out, which nothing between the read and the uses may do. What a lent
// register holds is therefore always held by a vector: it is given only elements and copies of lent
// registers.
#include "function_writer.h"

#include <algorithm>

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

// The register an instruction writes, or NONE.
uint32_t written_by(const Instruction &p_in) {
	const char first = ir::op_shape(p_in.op)[0];
	return (first == 'R' || first == 'r') && p_in.a != NONE && !first_is_read(p_in.op) ? p_in.a : NONE;
}

} // namespace

bool FunctionWriter::runs_code(const Instruction &p_in) const {
	if (p_in.op == Op::GetField && borrowed_[p_in.a] != NONE) {
		return false;
	}
	// What a register of a class whose objects end alone held is let go of with nothing else ending.
	const auto ends_alone = [&](uint32_t p_register) { return lent_[p_register] || ends_alone_[p_register]; };
	if (facts_ && p_in.op == Op::InitClass) {
		return !facts_->starts_quietly[p_in.a];
	}
	if (facts_ && p_in.op == Op::New) {
		// An object of such a class is made by the host out of memory alone: its constructor is a call of its own.
		return !(facts_->ends_alone[p_in.b] && ends_alone(p_in.a));
	}
	return may_run_code(module_, function_, p_in, lent_) && may_run_code(module_, function_, p_in, ends_alone_);
}

// The registers that are only ever given an element of a vector, or a copy of such a register: what
// `var b:Body = bodies[i]` is. They count nothing if the function is one in which nothing lets go of
// anything, or if each is only used at once after it is read (find_borrowed decides).
std::vector<bool> FunctionWriter::lent_candidates() const {
	const size_t count = function_.registers.size();
	std::vector<bool> candidate(count, false);
	for (uint32_t i = function_.parameters; i < count; i++) {
		candidate[i] = kind(i) == Kind::Obj && parts(i).empty() && alias_[i] == Alias::None;
	}
	for (const ir::Handler &handler : function_.handlers) {
		candidate[handler.reg] = false;
	}
	for (bool changed = true; changed;) {
		changed = false;
		for (const Instruction &in : function_.code) {
			const char first = ir::op_shape(in.op)[0];
			const bool writes = (first == 'R' || first == 'r') && in.a != NONE && !first_is_read(in.op);
			const uint32_t moved = changed_in_place(function_, in);
			if (moved != NONE && candidate[moved]) {
				candidate[moved] = false;
				changed = true;
			}
			if (writes && candidate[in.a] && !(in.op == Op::VecGet || (in.op == Op::Move && candidate[in.b]))) {
				candidate[in.a] = false;
				changed = true;
			}
		}
	}
	return candidate;
}

void FunctionWriter::find_field_refs() {
	field_refs_.assign(function_.registers.size(), false);
	std::vector<bool> other(function_.registers.size(), false);
	for (const auto &in : function_.code) {
		if (in.op == Op::Release && kind(in.a) == Kind::Ref && asked_[in.a] && owns(in.a)) {
			field_refs_[in.a] = true;
		}
		if (const auto written = written_by(in); written != NONE && in.op != Op::GetField) {
			other[written] = true;
		}
		if (in.op.execution != static_cast<Op>(in.op)) {
			other[in.op == Op::SetField || in.op == Op::VecSet ? in.c : in.b] = true;
		}
	}
	for (size_t i = 0; i < other.size(); ++i) {
		field_refs_[i] = field_refs_[i] && !other[i];
	}
}

void FunctionWriter::read_field(const Instruction &p_in, const std::string &p_field) {
	if (signal_from_[p_in.a] != NONE || value_from_[p_in.a] != NONE) {
		return; // The emit, or the operation on the value, that follows reads the field itself.
	}
	if (borrowed_[p_in.a] != NONE) {
		line(reg(p_in.a) + " = " + p_field + ";");
	} else if (field_refs_[p_in.a]) {
		line("field_ref" + std::to_string(p_in.a) + ".read(c, " + reg(p_in.a) + ", " + p_field + ");");
	} else if (kind(p_in.a) == Kind::Ref && asked_[p_in.a] && owns(p_in.a)) {
		// Read again in a loop, the field most often holds the object the register has a handle on already.
		line("store_again(c, " + reg(p_in.a) + ", " + p_field + ");");
	} else {
		share(p_in.a, kept_local(p_in.b, p_in.c).value_or(p_field));
	}
}

// Every read of a register that borrows follows a read of the field in straight code, with nothing
// between that could write the field or run code, and is itself of that kind. One that fails stops
// borrowing, which may make another's reads fail: until none does. p_at_once: the same is asked of a
// register that is lent, of the read of the element: one used only at once counts nothing in a function
// that does let go of things, since nothing takes an object out of a vector between the read and the use.
void FunctionWriter::settle_reads(bool p_at_once, const std::vector<bool> &p_lands) {
	for (bool changed = true; changed;) {
		changed = false;
		const size_t count = function_.registers.size();
		std::vector<bool> fresh(count, false);
		const auto stale = [&]() { std::fill(fresh.begin(), fresh.end(), false); };
		for (uint32_t pc = 0; pc < function_.code.size(); pc++) {
			const Instruction &in = function_.code[pc];
			if (p_lands[pc]) {
				stale();
			}
			// A call of a function that runs no code not looked into lets go of no object and stores none
			// (facts.h): what a register borrows or is lent is still held where it was. A call through a
			// method table is every function the table may hold there, or anything where they are not known.
			const FunctionFacts *callee = is_call(in.op) ? called(pc) : nullptr;
			const bool quiet = (!is_call(in.op) || (callee && !callee->opaque)) && !runs_code(in);
			// An object stored in a place of a vector whose elements end alone: what the place held is let go
			// of after the vector has been used, and with nothing run. The vector's register may borrow;
			// nothing lent is still good after it (the object let go of may be the one lent).
			const bool stores_alone = in.op == Op::VecSet && elements_end_alone(in.a);
			const auto read = [&](uint32_t p_register) {
				const bool usable = quiet || (stores_alone && p_register == in.a && p_register != in.c);
				if (borrowed_[p_register] != NONE && !(fresh[p_register] && usable)) {
					borrowed_[p_register] = NONE;
					changed = true;
				}
				if (p_at_once && lent_[p_register] && !(fresh[p_register] && quiet)) {
					lent_[p_register] = false;
					changed = true;
				}
			};
			// What a lent register holds is always held by a vector. A copy of a register that counts a
			// reference may hold what has left its vector, which that register alone keeps: the copy
			// counts one too.
			if (p_at_once && in.op == Op::Move && lent_[in.a] && !lent_[in.b]) {
				lent_[in.a] = false;
				changed = true;
			}
			const char *shape = ir::op_shape(in.op);
			const uint32_t operands[3] = { in.a, in.b, in.c };
			const uint32_t written = written_by(in);
			for (int n = 0; n < 3; n++) {
				if (shape[n] == 'A') {
					std::for_each(function_.argument_lists[operands[n]].begin(), function_.argument_lists[operands[n]].end(), read);
				} else if ((shape[n] == 'R' || shape[n] == 'r') && operands[n] != NONE && !(n == 0 && written != NONE)) {
					read(operands[n]);
				}
			}
			if (!quiet || in.op == Op::Jump || in.op == Op::JumpIf || in.op == Op::JumpIfNot) {
				stale();
			} else if (in.op == Op::SetField) {
				for (uint32_t i = 0; i < count; i++) {
					fresh[i] = fresh[i] && borrowed_[i] != in.b;
				}
			}
			if (written != NONE && (borrowed_[written] != NONE || (p_at_once && lent_[written]))) {
				fresh[written] = true;
			}
		}
	}
}

void FunctionWriter::find_borrowed() {
	if (!facts_ || !is_this(0)) {
		return;
	}
	const size_t count = function_.registers.size();
	const ir::Class &klass = module_.classes[function_.owner];
	std::vector<bool> refused(count, false);
	std::vector<bool> lands(function_.code.size() + 1, false);
	for (const ir::Handler &handler : function_.handlers) {
		refused[handler.reg] = true;
		lands[handler.target] = true;
	}
	// Every write of the register is a read of one object field of `this`.
	std::vector<uint32_t> slot(count, NONE);
	for (const Instruction &in : function_.code) {
		const uint32_t target = in.op == Op::Jump ? in.a : in.op == Op::JumpIf || in.op == Op::JumpIfNot ? in.b : NONE;
		if (target != NONE) {
			lands[target] = true;
		}
		if (const uint32_t changed = changed_in_place(function_, in); changed != NONE) {
			refused[changed] = true;
		}
		const uint32_t written = written_by(in);
		if (written == NONE) {
			continue;
		}
		const bool field = in.op == Op::GetField && in.b == 0 && in.c < klass.fields.size() && klass.fields[in.c].kind == Kind::Obj &&
				kind(written) == Kind::Obj && (slot[written] == NONE || slot[written] == in.c);
		slot[written] = field ? in.c : slot[written];
		refused[written] = refused[written] || !field;
	}
	for (uint32_t i = 0; i < count; i++) {
		borrowed_[i] = slot[i] != NONE && !refused[i] && i >= function_.parameters && owns(i) ? slot[i] : NONE;
	}
	// With the registers that would borrow and those that would be lent counting nothing, does anything in
	// the function still let go of something, or run code that might? If nothing does, no object leaves a
	// vector while the function runs, and the lent registers are, wherever they are read.
	const std::vector<uint32_t> hoped = borrowed_;
	const std::vector<bool> candidates = lent_candidates();
	lent_ = candidates;
	std::vector<bool> counts_nothing = lent_;
	for (uint32_t i = 0; i < count; i++) {
		counts_nothing[i] = counts_nothing[i] || borrowed_[i] != NONE;
	}
	bool calm = function_.handlers.empty();
	for (const Instruction &in : function_.code) {
		calm = calm && !is_call(in.op) && !may_run_code(module_, function_, in, counts_nothing);
	}
	if (calm) {
		settle_reads(false, lands);
		if (borrowed_ == hoped) {
			return;
		}
		// A register failed to borrow, and lets go of what it held: the function is not one that lets go
		// of nothing after all.
		borrowed_ = hoped;
		lent_ = candidates;
	}
	settle_reads(true, lands);
}

} // namespace backend_cpp
} // namespace kas3
