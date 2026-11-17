// The class an object register is known to hold, so that a field of it is written through the class's struct.
//
// `b.vx -= dx * a.mass` reads and writes fields of objects other than `this`. Written by the field's number
// alone (`field<double>(r4, 3)`), the C++ compiler is told only that a Number is written somewhere: it must
// then read every other Number field again after the write, in case that was the one, and it cannot keep
// two fields that sit side by side together. Written through the struct of the object's class
// (`static_cast<C10_Body *>(r4)->f3_vx`), it is told which field of which class: a write of `vx` leaves
// `mass` and `x` as they were, whichever objects the two registers hold, and one object reached through
// two registers is still one object to it, since both name the same field of the same struct.
//
// The class is what every write of the register agrees on: a parameter declared of the class, `this`, a
// new object, a field or a static field declared to hold the class, an element of a vector of it, `as`,
// a copy of such a register. Two classes leave their nearest common base; anything else (a call's
// result, an untyped value converted, an error caught) leaves none, and the field is reached by its
// number as before, which the C++ compiler takes for any field of any object.
#include "function_writer.h"

#include "names.h"
#include "program_writer.h"

namespace kas3 {
namespace backend_cpp {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

constexpr uint32_t UNSEEN = NONE - 1; // No write of the register has been looked at yet.

// The nearest class both are, or NONE. UNSEEN gives way to anything.
uint32_t shared_class(const ir::Module &p_module, uint32_t p_one, uint32_t p_other) {
	if (p_one == UNSEEN || p_other == UNSEEN) {
		return p_one == UNSEEN ? p_other : p_one;
	}
	for (uint32_t base = p_one; base != NONE; base = p_module.classes[base].base) {
		for (uint32_t each = p_other; each != NONE; each = p_module.classes[each].base) {
			if (each == base) {
				return base;
			}
		}
	}
	return NONE;
}

} // namespace

// The class of the objects an instruction writes into its first register, by what is known of the other
// registers so far: UNSEEN where that waits for a register not looked at yet, NONE where nothing is known.
uint32_t FunctionWriter::class_written(const Instruction &p_in, const std::vector<uint32_t> &p_known) const {
	const auto declared = [&](uint32_t p_class) { return p_class < module_.classes.size() ? p_class : NONE; };
	switch (p_in.op) {
		case Op::Move:
			return p_known[p_in.b];
		case Op::New:
		case Op::VecNew:
			return declared(p_in.b);
		case Op::AsClass:
			return declared(p_in.c);
		case Op::GetGlobal:
			return declared(module_.globals[p_in.b].klass);
		case Op::GetField: {
			const uint32_t of = p_known[p_in.b];
			if (of == UNSEEN || of == NONE) {
				return of;
			}
			return p_in.c < module_.classes[of].fields.size() ? declared(module_.classes[of].fields[p_in.c].klass) : NONE;
		}
		case Op::VecGet: {
			const uint32_t of = p_known[p_in.b];
			if (of == UNSEEN || of == NONE) {
				return of;
			}
			return module_.classes[of].element == Kind::Obj ? declared(module_.classes[of].element_check) : NONE;
		}
		default:
			return NONE;
	}
}

void FunctionWriter::find_classes() {
	const size_t count = function_.registers.size();
	std::vector<uint32_t> known(count, UNSEEN);
	for (uint32_t i = 0; i < function_.parameters; i++) {
		const bool self = i == 0 && function_.owner != NONE && !function_.is_static;
		known[i] = self ? function_.owner : i < function_.parameter_classes.size() ? function_.parameter_classes[i] : NONE;
		known[i] = known[i] < module_.classes.size() ? known[i] : NONE;
	}
	for (const ir::Handler &handler : function_.handlers) {
		known[handler.reg] = NONE;
	}
	for (bool changed = true; changed;) {
		changed = false;
		for (const Instruction &in : function_.code) {
			if (const uint32_t moved = changed_in_place(function_, in); moved != NONE && known[moved] != NONE) {
				known[moved] = NONE;
				changed = true;
			}
			const char first = ir::op_shape(in.op)[0];
			const bool writes = (first == 'R' || first == 'r') && in.a != NONE && !first_is_read(in.op);
			// `null` is of every class: it says nothing of what else the register holds.
			if (!writes || kind(in.a) != Kind::Obj || in.op == Op::ConstNull) {
				continue;
			}
			const uint32_t now = shared_class(module_, known[in.a], class_written(in, known));
			if (now != known[in.a]) {
				known[in.a] = now;
				changed = true;
			}
		}
	}
	class_of_.assign(count, NONE);
	for (uint32_t i = 0; i < count; i++) {
		class_of_[i] = known[i] == UNSEEN || kind(i) != Kind::Obj ? NONE : known[i];
	}
}

std::string FunctionWriter::field_of(uint32_t p_object, uint32_t p_slot, Kind p_kind) const {
	const uint32_t of = is_this(p_object) ? function_.owner : class_of_[p_object];
	if (of != NONE && has_struct(module_.classes[of]) && p_slot < module_.classes[of].fields.size() &&
			module_.classes[of].fields[p_slot].kind == p_kind) {
		return "static_cast<" + class_name(module_, of) + " *>(" + reg(p_object) + ")->" + field_name(module_.classes[of], p_slot);
	}
	return "field<" + std::string(type_of(p_kind)) + ">(" + reg(p_object) + ", " + std::to_string(p_slot) + ")";
}

} // namespace backend_cpp
} // namespace kas3
