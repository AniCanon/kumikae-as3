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

// What String(x) gives for an untyped value. A script object speaks for itself, which runs the program:
// the value is held while it does. False if that failed; its error is on its way out.
bool Interpreter::any_text(Slot p_value, std::string *r_text) {
	if (const std::optional<std::string> plain = any::text(p_value)) {
		*r_text += *plain;
		return true;
	}
	if (any::tag(p_value) == AnyTag::Object) {
		Object *object = static_cast<Object *>(any::pointer(p_value));
		retain(object);
		const bool said = object_as_text(object, r_text);
		release_object(object);
		return said;
	}
	return engine_text(any::pointer(p_value), r_text);
}

// Godot's text for one of its values or objects (null for no handle), by a handle of its own while the
// engine speaks, which can run a script. False if memory ran out, or if that script's toString() failed.
bool Interpreter::engine_text(void *p_handle, std::string *r_text) {
	const uint32_t carried_before = carries_;
	void *held = p_handle ? host_.handles.copy(p_handle) : nullptr;
	Str *text = held ? host_.handles.text(held) : nullptr;
	*r_text += text ? text->view() : std::string_view("null");
	if (release(text)) {
		free_string(text);
	}
	if (held) {
		host_.handles.drop(held);
		if (!text) {
			out_of_memory();
			return false;
		}
	}
	// A script's toString() Godot ran for this text failed: Godot's own text is not the value's.
	return carries_ == carried_before;
}

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

// An untyped value as a primitive, for `+` and `<`: an object or a Godot value becomes its text.
bool Interpreter::any_primitive(Slot p_value, Slot *r_out, bool p_number) {
	if (any::tag(p_value) != AnyTag::Object && !any::is_engine(p_value)) {
		*r_out = share(Kind::Any, p_value);
		return true;
	}
	const Object *object = any::tag(p_value) == AnyTag::Object ? static_cast<const Object *>(any::pointer(p_value)) : nullptr;
	if (p_number && object && object->klass == module_.date_class) {
		*r_out = any_number(const_cast<Object *>(object)->fields()[ir::DATE_TIME].d); // A Date's valueOf().
		return true;
	}
	// A class's own valueOf() comes first, if what it gives is not an object (ECMAScript's [[DefaultValue]]).
	const uint32_t value_of = object && module_.classes[object->klass].element == Kind::Void ? find_method(object->klass, "valueOf") : NONE;
	if (value_of != NONE) {
		Slot self;
		self.bits = 0;
		self.p = const_cast<Object *>(object);
		Slot result;
		result.bits = 0;
		retain(self.p ? static_cast<Object *>(self.p) : nullptr);
		const bool called = call_within(value_of, &self, 1, &result);
		const Kind kind = module_.functions[value_of].result;
		const Slot given = called && kind != Kind::Void ? to_any(kind, result) : any::undefined();
		if (called) {
			drop(kind, result);
		}
		release_object(static_cast<Object *>(self.p));
		if (!called) {
			return false;
		}
		if (any::tag(given) != AnyTag::Object && !any::is_engine(given)) {
			*r_out = given;
			return true;
		}
		drop_any(given);
	}
	std::string words;
	if (!any_text(p_value, &words)) {
		return false;
	}
	*r_out = any::string(text(words));
	return true;
}

Slot Interpreter::present_or_null(Slot p_value) {
	return any::is_engine(p_value) && !host_.handles.present(any::pointer(p_value)) ? any::null() : p_value;
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
			bool equal = false;
			if (any_equality(b, c, p_in.op == Op::AnyStrictEq, &equal)) {
				result(Kind::Bool, of_bool(equal));
			}
			break;
		}
		case Op::AnyAdd:
		case Op::AnyLt:
		case Op::AnyLe:
		case Op::AnyGt:
		case Op::AnyGe:
			any_arithmetic(p_in, b, c);
			break;
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

// AnyEq and AnyStrictEq. A Godot object that has been freed is null. Both are held only when one is a
// script object, whose text may run code that lets go of the other. False if it raised.
bool Interpreter::any_equality(Slot p_a, Slot p_b, bool p_strict, bool *r_equal) {
	const Slot first = present_or_null(p_a);
	const Slot second = present_or_null(p_b);
	const bool speaks = any::tag(first) == AnyTag::Object || any::tag(second) == AnyTag::Object;
	const Slot x = speaks ? share(Kind::Any, first) : first;
	const Slot y = speaks ? share(Kind::Any, second) : second;
	const bool done = any_equal(x, y, p_strict, r_equal);
	if (speaks) {
		drop_any(x);
		drop_any(y);
	}
	return done;
}

} // namespace vm
} // namespace kas3
