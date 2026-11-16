// Conversions the loop hands on: an Int64 to a narrower kind, or a Number to an Int64, checked; an object
// as text.
#include "interpreter_internal.h"

namespace kas3 {
namespace vm {

using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

// An Int64 where an int, a uint or a Number is expected, or a Number where an Int64 is, when the
// program did not write the conversion: the value, or a RangeError that names it.
void Interpreter::convert_checked(const Instruction &p_in, Slot *p_registers) {
	Slot &out = p_registers[p_in.a];
	const Slot &from = p_registers[p_in.b];
	switch (p_in.op) {
		case Op::I64ToI32:
			if (const std::optional<int32_t> fitted = wide_to_int32(from.l)) {
				set_int32(out, *fitted);
			} else {
				raise(module_.range_error_class, std::to_string(from.l) + " does not fit an int.");
			}
			break;
		case Op::I64ToU32:
			if (const std::optional<uint32_t> fitted = wide_to_uint32(from.l)) {
				set_uint32(out, *fitted);
			} else {
				raise(module_.range_error_class, std::to_string(from.l) + " does not fit a uint.");
			}
			break;
		case Op::I64ToF64:
			if (const std::optional<double> fitted = wide_to_number(from.l)) {
				out.d = *fitted;
			} else {
				raise(module_.range_error_class, std::to_string(from.l) + " does not fit a Number exactly.");
			}
			break;
		default:
			if (const std::optional<int64_t> fitted = number_to_wide(from.d)) {
				out.l = *fitted;
			} else {
				raise(module_.range_error_class, number_to_string(from.d) + " does not fit an Int64.");
			}
			break;
	}
}

// An object as text. A class that says how it reads has its toString() called, whose result lands in
// the register.
void Interpreter::object_to_text(const Instruction &p_in, Slot *p_registers) {
	Object *object = static_cast<Object *>(p_registers[p_in.b].p);
	const uint32_t own = object ? module_.classes[object->klass].to_string : NONE;
	if (object && module_.classes[object->klass].element != Kind::Void) {
		// A vector's elements, each in its own words: that may run the program and move the registers.
		retain(object);
		std::string all;
		const bool done = vector_text(static_cast<Vector *>(object), ",", &all);
		if (done) {
			assign(Kind::Str, stack_[frames_.back().base + p_in.a], text(all));
		}
		release_object(object);
	} else if (own != NONE) {
		Slot self = p_registers[p_in.b];
		push(own, &self, 1, p_in.a);
	} else {
		assign(Kind::Str, p_registers[p_in.a], text(object_text(object)));
	}
}

} // namespace vm
} // namespace kas3
