#include "interpreter_internal.h"

#include "text_units.h"
#include "vector_calls.h"

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

// The element's property of that name, or undefined when it has none (a value that is not an object, or
// an object without it): sortOn never raises #1069.
bool Interpreter::sort_field(Slot p_element, Str *p_name, Slot *r_value) {
	*r_value = any::undefined();
	if (!held_object(p_element)) {
		return true;
	}
	PropertyKey key;
	bool has = false;
	if (!property_key(p_element, any::string(p_name), &key) || !has_property(p_element, key, false, &has)) {
		return false;
	}
	return !has || get_property(p_element, key, r_value);
}

// VecCall: the method is the library's function of plain values, handed the values themselves (the
// registers may move as it runs the program); what it gives is stored where the register then is.
void Interpreter::vector_call(const Function &p_function, const Instruction &p_in) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const size_t base = frames_.back().base;
	SpareSlots given(spare_slots_);
	for (const uint32_t each : list) {
		given->push_back(stack_[base + each]);
	}
	Slot made;
	made.bits = 0;
	const bool done = vm::vector_call(library_, static_cast<ir::VectorMethod>(p_in.b), given->data(), given->size(), p_in.a == NONE ? nullptr : &made);
	if (done && p_in.a != NONE) {
		put(p_function.registers[p_in.a], stack_[frames_.back().base + p_in.a], made);
	}
}

// The instructions on a vector's elements and length. An element in range is read and written here; the
// rest (an index outside, a vector that grows, a new one) is the library's function.
void Interpreter::vector_op(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	Slot *r = p_registers;
	if (p_in.op == Op::ArrayNew || p_in.op == Op::VecNew) {
		const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
		Vector *made = nullptr;
		if (p_in.op == Op::VecNew) {
			made = vm::vector_new(library_, p_in.b, r[list[0]].u, r[list[1]].b);
		} else {
			SpareSlots given(spare_slots_);
			for (const uint32_t each : list) {
				given->push_back(r[each]);
			}
			made = vm::array_new(library_, p_in.b, given->data(), given->size());
		}
		if (made) {
			assign(Kind::Obj, r[p_in.a], made);
		}
		return;
	}
	const bool reads = p_in.op == Op::VecGet || p_in.op == Op::VecLength || p_in.op == Op::VecFixed;
	Vector *vector = static_cast<Vector *>(r[reads ? p_in.b : p_in.a].p);
	if (!vector) {
		raise_null();
		return;
	}
	const ir::Class &klass = module_.classes[vector->klass];
	const Kind kind = klass.element;
	switch (p_in.op.execution) {
		case Op::VecGet: {
			const Kind index = p_function.registers[p_in.c];
			// Arrays store tagged slots. The usual integer read can transfer one straight to its destination.
			if (kind == Kind::Any && index == Kind::I32 && r[p_in.c].i >= 0 && static_cast<uint32_t>(r[p_in.c].i) < vector->length) {
				heap_.copy_any(r[p_in.a], any::filled(slots_of(vector)[r[p_in.c].i]));
				break;
			}
			const std::optional<uint32_t> at = element_index(index, r[p_in.c], vector->length);
			if (at && vector->narrow) {
				r[p_in.a] = place(vector, *at); // An int or a uint: nothing is counted.
				break;
			}
			if (at && !vector->bare) {
				put(kind, r[p_in.a], share(kind, kind == Kind::Any ? any::filled(slots_of(vector)[*at]) : slots_of(vector)[*at]));
				break;
			}
			Slot made;
			made.bits = 0;
			if (vm::vector_get(library_, vector, index, r[p_in.c], &made)) {
				put(kind, r[p_in.a], made);
			}
			break;
		}
		case Op::VecLength:
			set_uint32(r[p_in.a], vector->length);
			break;
		case Op::VecSetLength:
			vm::vector_set_length(library_, vector, r[p_in.b].u);
			break;
		case Op::VecFixed:
			r[p_in.a] = of_bool(vector->fixed);
			break;
		case Op::VecSetFixed:
			vector_set_fixed(library_, vector, r[p_in.b].b);
			break;
		default:
			break;
	}
}

// Dispatch selects the transferring form before entering the store.
template <bool Transfer>
void Interpreter::vector_store(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	auto *vector = static_cast<Vector *>(p_registers[p_in.a].p);
	if (!vector) {
		raise_null();
		return;
	}
	const Kind index = p_function.registers[p_in.b];
	const Kind kind = module_.classes[vector->klass].element;
	const auto at = element_index(index, p_registers[p_in.b], vector->length);
	if (at && vector->narrow) {
		set_place(vector, *at, p_registers[p_in.c]);
	} else if (at && !vector->bare) {
		Slot &source = p_registers[p_in.c];
		put(kind, slots_of(vector)[*at], Transfer ? std::exchange(source, any::of_bits(0)) : share(kind, source));
	} else {
		const size_t source = frames_.back().base + p_in.c;
		vm::vector_set(library_, vector, index, p_registers[p_in.b], p_registers[p_in.c]);
		if constexpr (Transfer) {
			put(kind, stack_[source], any::of_bits(0));
		}
	}
}

template void Interpreter::vector_store<true>(const Function &, const Instruction &, Slot *);
template void Interpreter::vector_store<false>(const Function &, const Instruction &, Slot *);

// VecFrom, Vector.<T>(x), by the library's function: converting an element may run the program.
void Interpreter::vector_from(const Instruction &p_in) {
	const Slot source = share(Kind::Any, stack_[frames_.back().base + p_in.b]);
	Slot made;
	made.bits = 0;
	if (vm::vector_from(library_, p_in.c, source, &made)) {
		put(Kind::Obj, stack_[frames_.back().base + p_in.a], made);
	}
	drop_any(source);
}

} // namespace vm
} // namespace kas3
