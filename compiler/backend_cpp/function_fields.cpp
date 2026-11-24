// A field of the method's own object kept in a local.
//
// A field that a loop reads and writes through `this` goes through memory each time round, and a read
// that follows a write of the same place a few instructions on waits for it: the loop is as slow as that
// chain. So a number or a Boolean field of `this` that some loop uses, with nothing in that loop that
// could see or change it any other way, is a local of the function for the whole of it. The local is the
// field's value from the function's start to its end, and memory is brought up to date only where
// something else may look: before an instruction that may read the field any other way (the local is
// written back), after one that may write it (the local is read again), and where the function ends.
// What may: a call of a function that touches the slot or runs code nothing looks into (facts.h), the
// same slot through another register (which may be this very object), and anything that may run code.
#include "function_writer.h"

#include "names.h"

#include <set>

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

// The slot an instruction reads or writes through register 0, or NONE.
uint32_t own_slot(const Instruction &p_in) {
	if (p_in.op == Op::GetField && p_in.b == 0) {
		return p_in.c;
	}
	return p_in.op == Op::SetField && p_in.a == 0 ? p_in.b : NONE;
}

} // namespace

FunctionWriter::Touch FunctionWriter::touch(uint32_t p_pc, uint32_t p_slot) const {
	const Instruction &in = function_.code[p_pc];
	if (runs_code(in)) {
		return Touch::Writes;
	}
	if (is_call(in.op)) {
		const FunctionFacts *callee = called(p_pc);
		if (!callee || callee->opaque || may_be_field(module_, callee->writes, function_.owner, p_slot)) {
			return Touch::Writes;
		}
		return may_be_field(module_, callee->reads, function_.owner, p_slot) ? Touch::Reads : Touch::None;
	}
	if (in.op == Op::GetField && in.b != 0 && in.c == p_slot) {
		return Touch::Reads;
	}
	return in.op == Op::SetField && in.a != 0 && in.b == p_slot ? Touch::Writes : Touch::None;
}

void FunctionWriter::find_kept() {
	if (!facts_ || !is_this(0)) {
		return;
	}
	std::vector<std::pair<uint32_t, uint32_t>> loops; // From the instruction jumped back to, to the jump.
	std::set<uint32_t> slots;
	std::set<uint32_t> written;
	for (uint32_t pc = 0; pc < function_.code.size(); pc++) {
		const Instruction &in = function_.code[pc];
		const uint32_t target = in.op == Op::Jump ? in.a : in.op == Op::JumpIf || in.op == Op::JumpIfNot ? in.b : NONE;
		if (target != NONE && target <= pc) {
			loops.emplace_back(target, pc);
		}
		if (const uint32_t slot = own_slot(in); slot != NONE) {
			slots.insert(slot);
			if (in.op == Op::SetField) {
				written.insert(slot);
			}
		}
	}
	const ir::Class &klass = module_.classes[function_.owner];
	for (uint32_t slot : slots) {
		const Kind kind = slot < klass.fields.size() ? klass.fields[slot].kind : Kind::Void;
		if (kind != Kind::F64 && kind != Kind::I32 && kind != Kind::U32 && kind != Kind::Bool) {
			continue;
		}
		bool wanted = false;
		for (const auto &[from, to] : loops) {
			bool used = false;
			bool alone = true;
			for (uint32_t pc = from; pc <= to && alone; pc++) {
				used = used || own_slot(function_.code[pc]) == slot;
				alone = touch(pc, slot) == Touch::None;
			}
			wanted = wanted || (used && alone);
		}
		if (wanted) {
			kept_.push_back(Kept{ slot, written.count(slot) > 0 });
		}
	}
}

std::string FunctionWriter::own_field(uint32_t p_slot) const {
	return "static_cast<" + class_name(module_, function_.owner) + " *>(r0)->" + field_name(module_.classes[function_.owner], p_slot);
}

std::optional<std::string> FunctionWriter::kept_local(uint32_t p_object, uint32_t p_slot) const {
	for (const Kept &each : kept_) {
		if (p_object == 0 && each.slot == p_slot) {
			return "k" + std::to_string(p_slot);
		}
	}
	return std::nullopt;
}

std::string FunctionWriter::kept_declared() const {
	std::string out;
	for (const Kept &each : kept_) {
		const ir::Class &klass = module_.classes[function_.owner];
		out += "\t" + declared(klass.fields[each.slot].kind, "k" + std::to_string(each.slot)) + " = " + own_field(each.slot) + ";\n";
	}
	return out;
}

std::string FunctionWriter::kept_written_back(uint32_t p_pc) const {
	std::string out;
	for (const Kept &each : kept_) {
		const bool seen = p_pc == NONE || touch(p_pc, each.slot) != Touch::None;
		if (each.written && seen) {
			out += (out.empty() ? "" : " ") + own_field(each.slot) + " = k" + std::to_string(each.slot) + ";";
		}
	}
	return out;
}

std::string FunctionWriter::kept_read_again(uint32_t p_pc) const {
	std::string out;
	for (const Kept &each : kept_) {
		if (p_pc == NONE || touch(p_pc, each.slot) == Touch::Writes) {
			out += (out.empty() ? "" : " ") + ("k" + std::to_string(each.slot) + " = " + own_field(each.slot) + ";");
		}
	}
	return out;
}

void FunctionWriter::keep(const std::string &p_place, uint32_t p_register, bool p_is_slot) {
	if (!hands_over_[pc_]) {
		line(std::string(p_is_slot ? "store_slot" : "store") + "(c, " + p_place + ", " + reg(p_register) + ");");
		return;
	}
	if (p_is_slot) {
		// What the slot held is let go of as the kind it is of, last.
		line("{ const Slot old = " + p_place + "; " + p_place + " = slot(" + reg(p_register) + "); release(c, " + from_slot(kind(p_register), "old") + "); }");
	} else {
		line("put(c, " + p_place + ", " + reg(p_register) + ");");
	}
	line(reg(p_register) + " = " + std::string(zero_of(kind(p_register))) + ";");
}

} // namespace backend_cpp
} // namespace kas3
