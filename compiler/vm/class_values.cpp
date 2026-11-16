// Classes as values: `var c:Class = Player`, `x is c`, `new c(a)`. A class value is an object of the
// module's Class class that says which class it is (ir::CLASS_CODE): there is one for each class.
#include "library_calls.h"

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

bool is_value_code(uint32_t p_code) {
	return p_code >= ir::ANY_IS_INT && p_code <= ir::ANY_IS_OBJECT;
}

} // namespace

Object *Interpreter::class_value(uint32_t p_code) {
	if (const auto found = class_values_.find(p_code); found != class_values_.end()) {
		return found->second;
	}
	Object *made = module_.class_class == NONE ? nullptr : new_object(module_.class_class, slots_of(module_.classes[module_.class_class]), heap_.list_for(module_.class_class));
	if (!made) {
		out_of_memory();
		return nullptr;
	}
	own_field(made, ir::CLASS_CODE).bits = 0;
	own_field(made, ir::CLASS_CODE).i = static_cast<int32_t>(p_code);
	class_values_.emplace(p_code, made); // Its one reference, kept until the interpreter goes.
	return made;
}

std::optional<uint32_t> Interpreter::class_in(Slot p_class, std::string_view p_error) {
	const Object *object = held_object(p_class);
	if (object && module_.class_class != NONE && object->klass == module_.class_class) {
		return static_cast<uint32_t>(own_field(const_cast<Object *>(object), ir::CLASS_CODE).i);
	}
	if (!object && (any::tag(p_class) == AnyTag::Null || any::tag(p_class) == AnyTag::Undefined)) {
		raise_null();
	} else {
		raise(module_.type_error_class, std::string(p_error));
	}
	return std::nullopt;
}

void Interpreter::class_op(const Function &p_function, const Instruction &p_in) {
	const size_t base = frames_.back().base;
	if (p_in.op == Op::ClassValue) {
		if (Object *value = class_value(p_in.b)) {
			put(Kind::Obj, stack_[base + p_in.a], share(Kind::Obj, of_object(value)));
		}
		return;
	}
	if (p_in.op == Op::ClassNew) {
		const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
		put(Kind::Any, stack_[base + p_in.a], any::undefined());
		const std::optional<uint32_t> code = class_in(stack_[base + p_in.b], "Error #1007: Instantiation attempted on a non-constructor.");
		if (!code) {
			return;
		}
		SpareSlots given(spare_slots_);
		for (const uint32_t each : list) {
			given->push_back(share(Kind::Any, stack_[base + each]));
		}
		class_new(*code, *given, p_in.a);
		for (Slot each : *given) {
			drop_any(each);
		}
		return;
	}
	const bool chain = p_in.op == Op::ClassInstanceOf;
	const std::optional<uint32_t> code = class_in(stack_[base + p_in.c],
			chain ? "Error #1040: The right-hand side of instanceof must be a class or function."
				  : "Error #1041: The right-hand side of operator must be a class.");
	if (!code) {
		return;
	}
	const Slot value = present_or_null(stack_[base + p_in.b]);
	// instanceof: an interface is in no chain of prototypes, and a number's prototype is Number's.
	const bool never = chain && (*code == ir::ANY_IS_INT || *code == ir::ANY_IS_UINT || (*code < module_.classes.size() && module_.classes[*code].is_interface));
	const bool is = !never && any_is(value, *code);
	if (p_in.op == Op::ClassAs) {
		put(Kind::Any, stack_[base + p_in.a], is ? share(Kind::Any, value) : any::null());
	} else {
		put(Kind::Bool, stack_[base + p_in.a], of_bool(is));
	}
}

// `new c(a)`: what `new` of the class named makes, the arguments converted and counted as a call's are.
void Interpreter::class_new(uint32_t p_code, const std::vector<Slot> &p_given, uint32_t p_out) {
	const auto give = [&](Kind p_kind, Slot p_owned) {
		put(Kind::Any, stack_[frames_.back().base + p_out], to_any(p_kind, p_owned));
		drop(p_kind, p_owned);
	};
	const auto count_error = [&](size_t p_most) {
		raise(module_.argument_error_class != NONE ? module_.argument_error_class : module_.error_class,
				"Error #1063: Argument count mismatch on " + class_name(p_code) + "(). Expected " + std::to_string(p_most) + ", got " +
						std::to_string(p_given.size()) + ".");
	};
	const ir::Class *klass = p_code < module_.classes.size() ? &module_.classes[p_code] : nullptr;
	const bool object = p_code == ir::ANY_IS_OBJECT || (klass && klass->table && !klass->dictionary);
	if (object || is_value_code(p_code)) {
		// A number, a Boolean, a String: the conversion of what is given, or the type's empty value. An
		// Object: what is given, or a new one for nothing, null or undefined.
		if (p_given.size() > 1) {
			count_error(1);
			return;
		}
		const AnyTag tag = p_given.empty() ? AnyTag::Undefined : any::tag(p_given[0]);
		if (object && (tag == AnyTag::Undefined || tag == AnyTag::Null)) {
			Object *made = module_.object_class == NONE ? nullptr : new_table(module_.object_class, heap_.list_for(module_.object_class));
			if (made) {
				give(Kind::Obj, of_object(made));
			} else {
				out_of_memory();
			}
			return;
		}
		const Kind kind = p_code == ir::ANY_IS_INT ? Kind::I32 : p_code == ir::ANY_IS_UINT ? Kind::U32 : p_code == ir::ANY_IS_NUMBER ? Kind::F64
				: p_code == ir::ANY_IS_BOOLEAN ? Kind::Bool : p_code == ir::ANY_IS_STRING ? Kind::Str : Kind::Any;
		Slot value;
		value.bits = 0;
		std::string words;
		if (kind == Kind::Str) {
			// String(x): "null" and "undefined" are words; nothing is the empty text.
			if (!p_given.empty() && !any_text(p_given[0], &words)) {
				return;
			}
			value.p = text(words);
			if (!value.p) {
				return;
			}
		} else if (!from_any(p_given.empty() ? any::integer(0) : p_given[0], kind, NONE, &value)) {
			return;
		}
		give(kind, value);
		return;
	}
	if (!klass || klass->is_interface || p_code == module_.function_class || p_code == module_.class_class) {
		raise(module_.type_error_class, "Error #1007: Instantiation attempted on a non-constructor.");
		return;
	}
	if (klass->element != Kind::Void || p_code == module_.date_class || p_code == module_.regexp_class) {
		// An Array, a Vector, a Date, a RegExp: the operation a typed `new` of it runs.
		std::vector<Kind> kinds;
		std::vector<Slot> values;
		const bool vector = klass->element != Kind::Void && !klass->array;
		const bool parts = p_code == module_.date_class && p_given.size() > 1;
		const size_t most = vector || p_code == module_.regexp_class ? 2 : p_code == module_.date_class ? 7 : SIZE_MAX;
		if (p_given.size() > most) {
			count_error(most);
			return;
		}
		bool ok = true;
		for (size_t i = 0; ok && i < (vector || p_code == module_.regexp_class ? 2 : p_given.size()); i++) {
			const Kind kind = vector ? (i == 0 ? Kind::U32 : Kind::Bool) : parts ? Kind::F64 : Kind::Any;
			const Slot given = i < p_given.size() ? p_given[i] : vector ? any::integer(0) : any::undefined();
			kinds.push_back(kind);
			values.emplace_back();
			values.back().bits = 0;
			ok = from_any(given, kind, NONE, &values.back());
		}
		if (!ok) {
			for (size_t i = 0; i < values.size(); i++) {
				drop(kinds[i], values[i]);
			}
			return;
		}
		const Op op = klass->array ? Op::ArrayNew : vector ? Op::VecNew : p_code == module_.date_class ? Op::DateCall : Op::RegExpCall;
		const uint32_t which = op == Op::DateCall ? ir::date_code(ir::DateFunction::New)
				: op == Op::RegExpCall ? static_cast<uint32_t>(ir::RegExpFunction::New) : p_code;
		if (const std::optional<Slot> made = LibraryCall::run_typed(*this, op, which, kinds, values, Kind::Obj)) {
			give(Kind::Obj, *made);
		}
		return;
	}
	if (klass->table) {
		// A Dictionary: weak keys are refused where a typed `new` refuses them.
		if (p_given.size() > 1) {
			count_error(1);
		} else if (!p_given.empty() && any::to_boolean(p_given[0])) {
			raise_unsupported("a Dictionary with weak keys");
		} else if (Object *made = new_table(p_code, heap_.list_for(p_code))) {
			give(Kind::Obj, of_object(made));
		} else {
			out_of_memory();
		}
		return;
	}
	// A class of the program, or an Error: the object, then its constructor. The class's static fields
	// are set up first: pushed last, that runs before the constructor does.
	Slot made;
	made.bits = 0;
	make(p_code, made);
	if (!made.p) {
		return;
	}
	Object *fresh = static_cast<Object *>(made.p);
	put(Kind::Any, stack_[frames_.back().base + p_out], any::object(fresh)); // The register's reference is the one make() gave.
	if (klass->constructor != NONE) {
		call_with(klass->constructor, fresh, p_given, NONE);
	} else if (!p_given.empty()) {
		count_error(0);
	}
	if (!failing_ && p_code < class_used_.size() && !class_used_[p_code]) {
		use_class(p_code);
	}
}

} // namespace vm
} // namespace kas3
