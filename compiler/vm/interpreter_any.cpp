#include "interpreter_internal.h"

#include <cmath>
#include <limits>

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

// Whether a value is one of Godot's Arrays and the class is Array: AS3 takes one as an Array of its own.
bool Interpreter::engine_array(Slot p_value, uint32_t p_class) const {
	return any::tag(p_value) == AnyTag::Value && p_class < module_.classes.size() && module_.classes[p_class].array &&
			host_.handles.is_class(any::pointer(p_value), "Array");
}

Object *Interpreter::copy_engine_array(void *p_array, uint32_t p_class) {
	Vector *made = array_of_engine(p_array, p_class);
	if (!made) {
		report();
	}
	return made;
}

// A new Array of the elements of one of Godot's Arrays, each untyped: a copy, as a Vector given to Godot
// is copied. Null, with the error raised, if the engine could not give them.
Vector *Interpreter::array_of_engine(void *p_array, uint32_t p_class) {
	std::string error;
	Wanted whole;
	whole.kind = Kind::I64;
	Slot size;
	size.bits = 0;
	if (!host_.handles.call(p_array, "size", nullptr, 0, whole, &size, &error)) {
		raise(module_.error_class, error);
		return nullptr;
	}
	Vector *made = fresh_vector(p_class, size.l < 0 ? 0 : static_cast<uint64_t>(size.l));
	Wanted untyped;
	untyped.kind = Kind::Any;
	for (uint32_t i = 0; made && i < made->length; i++) {
		Argument key;
		key.kind = Kind::I64;
		key.slot.bits = 0;
		key.slot.l = i;
		if (!host_.handles.index_get(p_array, key, untyped, &made->data[i], &error)) {
			release_object(made);
			raise(module_.error_class, error);
			return nullptr;
		}
	}
	return made;
}

// One of the untyped value's operations. Anything that runs the program (an object's text) may move the
// registers: the operands are copied first, and the result is written through the frame's place again.
void Interpreter::any_op(const Function &p_function, const Instruction &p_in, Slot *p_registers) {
	const size_t base = frames_.back().base;
	const auto result = [&](Kind p_kind, Slot p_owned) { put(p_kind, stack_[base + p_in.a], p_owned); };
	// Only the operands the operation's shape says are registers are read.
	const Slot b = ir::op_shape(p_in.op)[1] == 'R' ? p_registers[p_in.b] : Slot{};
	const Slot c = ir::op_shape(p_in.op)[2] == 'R' ? p_registers[p_in.c] : Slot{};
	switch (p_in.op) {
		case Op::AnyConst:
			result(Kind::Any, p_in.b ? any::null() : any::undefined());
			break;
		case Op::ToAny:
			result(Kind::Any, to_any(p_function.registers[p_in.b], b));
			break;
		case Op::FromAny: {
			// An object that speaks for itself is held while it does, by any_text().
			const Kind kind = p_function.registers[p_in.a];
			Slot out;
			if (from_any(b, kind, p_in.c, &out)) {
				result(kind, out);
			}
			break;
		}
		case Op::AnyText: {
			std::string words;
			if (any_text(b, &words)) {
				Slot out;
				out.bits = 0;
				out.p = text(words);
				result(Kind::Str, out);
			}
			break;
		}
		case Op::AnyTypeof:
			result(Kind::Str, type_name(b));
			break;
		case Op::AnyEq:
		case Op::AnyStrictEq: {
			const Slot both[] = { b, c };
			Slot equal;
			if (vm::any_operate(library_, p_in.op, both, 2, &equal)) {
				result(Kind::Bool, equal);
			}
			break;
		}
		case Op::AnyAdd:
		case Op::AnyLt:
		case Op::AnyLe:
		case Op::AnyGt:
		case Op::AnyGe:
		{
			const Slot both[] = { b, c };
			Slot made;
			if (vm::any_operate(library_, p_in.op, both, 2, &made)) {
				result(p_in.op == Op::AnyAdd ? Kind::Any : Kind::Bool, made);
			}
			break;
		}
		case Op::AnyIs:
			result(Kind::Bool, of_bool(any_is(b, p_in.c)));
			break;
		case Op::AnyIsGodot:
			result(Kind::Bool, of_bool(any::tag(b) == AnyTag::Godot && host_.handles.is_class(any::pointer(b), module_.engine_names[p_in.c].name)));
			break;
		default:
			break;
	}
}

// What typeof gives for an untyped value, as a String with a reference of its own: one of the names made
// once, in type_names_.
Slot Interpreter::type_name(Slot p_value) {
	Slot out;
	out.bits = 0;
	const bool function = any::tag(p_value) == AnyTag::Object && static_cast<const Object *>(any::pointer(p_value))->klass == module_.function_class;
	const std::string_view name = function ? std::string_view("function") : any::type_of(p_value);
	for (Str *each : type_names_) {
		out.p = each->view() == name ? each : out.p;
	}
	retain(static_cast<Str *>(out.p));
	return out;
}

} // namespace vm
} // namespace kas3
