// A field of `this` that holds an object, read into a register only to be used at once.
//
// `cells[i]`, where cells is a field, reads the field into a register and then the element. The register
// held a reference of its own for that: one counted and one given back each time, in the middle of a loop,
// and a register being let go of is something the other rules (a field in a local, a loop written twice)
// must stop for. But the field itself holds the object for as long as nothing writes the field, so a
// register that is only ever read from one such field, and only used in straight code right after with
// nothing between that could write the field or run other code, borrows: it counts nothing and lets go of
// nothing. What uses it must itself run no code (an element read or written, a length, a comparison, a
// return): a call that took it would have nothing keeping the object if the callee wrote the field.
#include "function_writer.h"

#include <algorithm>

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

bool FunctionWriter::runs_code(const Instruction &p_in) const {
	return may_run_code(module_, function_, p_in, lent_) && !(p_in.op == Op::GetField && borrowed_[p_in.a] != NONE);
}

// The registers that are only ever given an element of a vector, or a copy of such a register: what
// `var b:Body = bodies[i]` is. They count nothing if the function is one in which nothing lets go of
// anything (find_borrowed decides).
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

void FunctionWriter::read_field(const Instruction &p_in, const std::string &p_field) {
	if (borrowed_[p_in.a] != NONE) {
		line(reg(p_in.a) + " = " + p_field + ";");
	} else {
		share(p_in.a, kept_local(p_in.b, p_in.c).value_or(p_field));
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
	const auto writes = [&](const Instruction &p_in) {
		const char first = ir::op_shape(p_in.op)[0];
		return (first == 'R' || first == 'r') && p_in.a != NONE && !first_is_read(p_in.op) ? p_in.a : NONE;
	};
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
		const uint32_t written = writes(in);
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
	// vector while the function runs, and the lent registers are. (A register that then fails to borrow,
	// below, lets go of what it held: the lent ones are given up, and the others found again without them.)
	const std::vector<uint32_t> hoped = borrowed_;
	lent_ = lent_candidates();
	std::vector<bool> counts_nothing = lent_;
	for (uint32_t i = 0; i < count; i++) {
		counts_nothing[i] = counts_nothing[i] || borrowed_[i] != NONE;
	}
	bool calm = function_.handlers.empty();
	for (const Instruction &in : function_.code) {
		calm = calm && in.op != Op::Call && !may_run_code(module_, function_, in, counts_nothing);
	}
	if (!calm) {
		lent_.assign(count, false);
	}
	// Every read of it follows a read of the field in straight code, with nothing between that could write
	// the field or run code, and is itself of that kind. One that fails stops borrowing, which may make
	// another's reads fail: until none does.
	const auto settle_reads = [&]() {
		for (bool changed = true; changed;) {
			changed = false;
			std::vector<bool> fresh(count, false);
			const auto stale = [&]() { std::fill(fresh.begin(), fresh.end(), false); };
			for (uint32_t pc = 0; pc < function_.code.size(); pc++) {
				const Instruction &in = function_.code[pc];
				if (lands[pc]) {
					stale();
				}
				const bool quiet = in.op != Op::Call && !runs_code(in);
				const auto read = [&](uint32_t p_register) {
					if (borrowed_[p_register] != NONE && !(fresh[p_register] && quiet)) {
						borrowed_[p_register] = NONE;
						changed = true;
					}
				};
				const char *shape = ir::op_shape(in.op);
				const uint32_t operands[3] = { in.a, in.b, in.c };
				const uint32_t written = writes(in);
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
				if (written != NONE && borrowed_[written] != NONE) {
					fresh[written] = true;
				}
			}
		}
	};
	settle_reads();
	if (calm && borrowed_ != hoped) {
		lent_.assign(count, false);
		borrowed_ = hoped;
		settle_reads();
	}
}

} // namespace backend_cpp
} // namespace kas3
