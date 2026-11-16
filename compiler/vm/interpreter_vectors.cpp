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

namespace {

// Whether two elements are the same value, as `===` has it.
bool same_element(Kind p_kind, Slot p_a, Slot p_b) {
	switch (p_kind) {
		case Kind::F64:
			return p_a.d == p_b.d;
		case Kind::Bool:
			return p_a.b == p_b.b;
		case Kind::I64:
			return p_a.l == p_b.l;
		case Kind::Str: {
			const Str *a = static_cast<const Str *>(p_a.p);
			const Str *b = static_cast<const Str *>(p_b.p);
			return a && b ? a->view() == b->view() : a == b;
		}
		case Kind::Obj:
			return p_a.p == p_b.p;
		default:
			return p_a.u == p_b.u;
	}
}

} // namespace

// Whether an element is the value looked for. One of Godot's values or objects is the same as Godot says:
// the same object, or an equal value. Null is only null.
bool Interpreter::element_is(Kind p_kind, Slot p_element, Slot p_value) {
	if (p_kind == Kind::Any) {
		bool equal = false;
		return any_equal(p_element, p_value, true, &equal) && equal;
	}
	if (!ir::is_engine(p_kind)) {
		return same_element(p_kind, p_element, p_value);
	}
	if (!p_element.p || !p_value.p) {
		return p_element.p == p_value.p;
	}
	const Argument both[2] = { Argument{ p_kind, p_element }, Argument{ p_kind, p_value } };
	Wanted wanted;
	wanted.kind = Kind::Bool;
	Slot equal;
	equal.bits = 0;
	std::string error;
	const OperatorName same = operator_named(module_.equal_operator, "==");
	return host_.handles.operate(same.number, same.spelling, both, 2, wanted, &equal, &error) && equal.b;
}

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
	const Kind kind = module_.classes[vector->klass].element;
	switch (p_in.op) {
		case Op::VecGet: {
			const Kind index = p_function.registers[p_in.c];
			if (const std::optional<uint32_t> at = element_index(index, r[p_in.c], vector->length)) {
				put(kind, r[p_in.a], share(kind, vector->data[*at]));
				break;
			}
			Slot made;
			made.bits = 0;
			if (vm::vector_get(library_, vector, index, r[p_in.c], &made)) {
				put(kind, r[p_in.a], made);
			}
			break;
		}
		case Op::VecSet: {
			const Kind index = p_function.registers[p_in.b];
			if (const std::optional<uint32_t> at = element_index(index, r[p_in.b], vector->length)) {
				put(kind, vector->data[*at], share(kind, r[p_in.c]));
			} else {
				vm::vector_set(library_, vector, index, r[p_in.b], r[p_in.c]);
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
			vector->fixed = r[p_in.b].b;
			break;
		default:
			break;
	}
}

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
