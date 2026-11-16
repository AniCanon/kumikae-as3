#include "library_calls.h"

#include <numeric>

namespace kas3 {
namespace vm {

using ir::Does;
using ir::EachMethod;
using ir::Kind;
using ir::NONE;
using ir::Taken;
using ir::Takes;

bool Interpreter::library_call(Slot p_holder, std::string_view p_name, const std::vector<Slot> &p_given, uint32_t p_out) {
	const ir::LibraryMethod *method = LibraryCall::find(*this, p_holder, p_name);
	if (method) {
		LibraryCall(*this, *method, p_holder, p_given, p_out).run();
	}
	return method != nullptr;
}

bool Interpreter::library_name(Slot p_holder, std::string_view p_name) const {
	return LibraryCall::find(*this, p_holder, p_name) != nullptr;
}

bool Interpreter::method_value(Slot p_holder, std::string_view p_name, Slot *r_value) {
	if (module_.library_function == NONE || module_.array_class == NONE) {
		raise_unsupported("reading '" + std::string(p_name) + "' of " + holder_name(p_holder) + " by its name");
		return false;
	}
	Vector *bound = fresh_vector(module_.array_class, 2);
	Str *name = bound ? text(std::string(p_name)) : nullptr;
	if (!name) {
		if (bound) {
			release_object(bound);
		}
		return false;
	}
	bound->data[0] = share(Kind::Any, p_holder);
	bound->data[1] = any::string(name);
	Object *made = make_function(module_.library_function, bound);
	release_object(bound); // The function holds it now.
	*r_value = made ? any::object(made) : any::undefined();
	return made != nullptr;
}

// CallLibrary: the method a function value is bound to, called by its name with the arguments given.
void Interpreter::call_library(const ir::Instruction &p_in) {
	const size_t base = frames_.back().base;
	const Vector *bound = static_cast<const Vector *>(stack_[base + p_in.b].p);
	const Vector *rest = static_cast<const Vector *>(stack_[base + p_in.c].p);
	put(Kind::Any, stack_[base + p_in.a], any::undefined());
	const Slot holder = share(Kind::Any, bound->data[0]);
	const Slot name = share(Kind::Any, bound->data[1]);
	SpareSlots given(spare_slots_);
	for (uint32_t i = 0; rest && i < rest->length; i++) {
		given->push_back(share(Kind::Any, rest->data[i]));
	}
	PropertyKey key;
	if (property_key(holder, name, &key)) {
		call_named(holder, key, *given, p_in.a);
	}
	drop_any(holder);
	drop_any(name);
	for (Slot each : *given) {
		drop_any(each);
	}
}

const ir::LibraryMethod *LibraryCall::find(const Interpreter &p_vm, Slot p_holder, std::string_view p_name) {
	uint8_t on = 0;
	if (any::tag(p_holder) == AnyTag::String) {
		on = ir::ON_TEXT;
	} else if (const Object *object = p_vm.held_object(p_holder)) {
		const ir::Class &klass = p_vm.module_.classes[object->klass];
		on = klass.array ? ir::ON_ARRAY : klass.element != Kind::Void ? ir::ON_VECTOR : object->klass == p_vm.module_.function_class ? ir::ON_FUNCTION : 0;
	}
	return on ? ir::library_method(p_name, on) : nullptr;
}

void LibraryCall::run() {
	if (!counted()) {
		return;
	}
	switch (method_.does) {
		case Does::Each:
			each();
			break;
		case Does::Invoke:
			invoke();
			break;
		default:
			typed();
			break;
	}
}

const ir::Class *LibraryCall::list_class() const {
	const Object *object = vm_.held_object(holder_);
	const ir::Class *klass = object ? &vm_.module_.classes[object->klass] : nullptr;
	return klass && klass->element != Kind::Void ? klass : nullptr;
}

bool LibraryCall::counted() const {
	size_t most = 0, least = 0;
	for (const Taken &taken : method_.fixed) {
		most += taken.kind != Takes::Nothing ? 1 : 0;
		least += taken.kind != Takes::Nothing && taken.required ? 1 : 0;
	}
	const size_t count = given_.size();
	if (count >= least && (count <= most || method_.rest != Takes::Nothing)) {
		return true;
	}
	vm_.raise(vm_.module_.argument_error_class != NONE ? vm_.module_.argument_error_class : vm_.module_.error_class,
			"Error #1063: Argument count mismatch on " + vm_.holder_name(holder_) + "/" + std::string(method_.name) + "(). Expected " +
					std::to_string(least) + ", got " + std::to_string(count) + ".");
	return false;
}

bool LibraryCall::converted(const Taken *p_fixed, Takes p_takes, size_t p_index, Kind *r_kind, Slot *r_value) {
	const ir::Class *klass = list_class();
	*r_kind = ir::kind_taken(p_takes, klass ? klass->element : Kind::Void);
	r_value->bits = 0;
	const bool left_out = p_index >= given_.size();
	const Slot given = left_out ? any::undefined() : given_[p_index];
	const bool text = p_takes == Takes::Text || p_takes == Takes::Words;
	if (text && !method_.text_left_out.empty() && left_out) {
		// Only one left out: undefined given is null, as a store into a String makes it (avmplus's coerce_s).
		r_value->p = vm_.text(std::string(method_.text_left_out));
		return r_value->p != nullptr;
	}
	if (left_out && p_takes == Takes::Any) {
		*r_value = p_fixed && p_fixed->left_out != 0 ? any::null() : any::undefined();
		return true;
	}
	if (left_out) {
		const Slot number = any::number(p_fixed ? p_fixed->left_out : 0);
		return text || vm_.from_any(number, *r_kind, NONE, r_value); // A String left out with no default is null.
	}
	if (p_takes == Takes::Words) {
		std::string words;
		const Slot held = vm_.share(Kind::Any, given);
		const bool said = vm_.any_text(held, &words);
		vm_.drop_any(held);
		r_value->p = said ? vm_.text(words) : nullptr;
		return r_value->p != nullptr;
	}
	if (p_takes == Takes::Sought && *r_kind == Kind::Any) {
		// Looked for as it is: nothing is converted, but for undefined in a Vector.<Object>, which is null.
		return vm_.from_any(given, Kind::Any, klass && klass->element == Kind::Any ? klass->element_check : NONE, r_value);
	}
	uint32_t check = NONE;
	if (p_takes == Takes::Element) {
		check = klass->element_check;
	} else if (p_takes == Takes::Own) {
		check = vm_.held_object(holder_)->klass;
	}
	return vm_.from_any(given, *r_kind, check, r_value);
}

void LibraryCall::give(Kind p_kind, Slot p_owned) {
	if (out_ != NONE && p_kind != Kind::Void) {
		vm_.put(Kind::Any, vm_.stack_[vm_.frames_.back().base + out_], vm_.to_any(p_kind, p_owned));
	}
	vm_.drop(p_kind, p_owned);
}

// The row's operation, run on registers set past the running frame's: what it is called on, each argument
// converted, and one more for what it gives. The operation is done before this returns (none of these
// leaves a call for the loop to carry on with), so the registers are gone again after it.
void LibraryCall::typed() {
	const ir::Class *klass = list_class();
	std::vector<Kind> kinds;
	std::vector<Slot> values;
	const Kind holder = klass ? Kind::Obj : Kind::Str;
	kinds.push_back(holder);
	values.push_back(vm_.share(holder, of_object(any::pointer(holder_))));
	bool ok = true;
	size_t at = 0;
	for (; ok && at < method_.fixed.size() && method_.fixed[at].kind != Takes::Nothing; at++) {
		kinds.emplace_back();
		values.emplace_back();
		ok = converted(&method_.fixed[at], method_.fixed[at].kind, at, &kinds.back(), &values.back());
	}
	for (; ok && method_.rest != Takes::Nothing && at < given_.size(); at++) {
		kinds.emplace_back();
		values.emplace_back();
		ok = converted(nullptr, method_.rest, at, &kinds.back(), &values.back());
	}
	if (!ok) {
		for (size_t i = 0; i < values.size(); i++) {
			vm_.drop(kinds[i], values[i]);
		}
		return;
	}
	const Kind result = ir::kind_taken(method_.result, klass ? klass->element : Kind::Void);
	const ir::Op op = method_.does == Does::VecCall ? ir::Op::VecCall : method_.does == Does::StrCall ? ir::Op::StrCall : ir::Op::RegExpCall;
	const uint32_t which = klass && method_.does == Does::VecCall ? static_cast<uint32_t>(ir::search_untyped(static_cast<ir::VectorMethod>(method_.which), klass->element))
																  : method_.which;
	if (const std::optional<Slot> made = run_typed(vm_, op, which, kinds, values, result)) {
		give(result, *made);
	}
}

std::optional<Slot> LibraryCall::run_typed(Interpreter &p_vm, ir::Op p_op, uint32_t p_which, const std::vector<Kind> &p_kinds,
		std::vector<Slot> &p_values, Kind p_result) {
	const size_t base = p_vm.frames_.back().base;
	const uint32_t first = static_cast<uint32_t>(p_vm.stack_.size() - base);
	const size_t count = p_values.size();
	Slot zero;
	zero.bits = 0;
	p_vm.stack_.resize(p_vm.stack_.size() + count + 1, zero);
	std::copy(p_values.begin(), p_values.end(), p_vm.stack_.begin() + static_cast<std::ptrdiff_t>(base + first));
	// The instruction, as the lowerer would have written it for a typed call, and enough of a function
	// for it to be read from: the registers' kinds and the list of its arguments.
	ir::Function scratch;
	scratch.registers.assign(first, Kind::I32);
	scratch.registers.insert(scratch.registers.end(), p_kinds.begin(), p_kinds.end());
	scratch.registers.push_back(p_result == Kind::Void ? Kind::I32 : p_result);
	scratch.argument_lists.emplace_back(count);
	std::iota(scratch.argument_lists[0].begin(), scratch.argument_lists[0].end(), first);
	ir::Instruction in;
	in.op = p_op;
	in.a = p_result == Kind::Void ? NONE : first + static_cast<uint32_t>(count);
	in.b = p_which;
	in.c = 0;
	switch (p_op) {
		case ir::Op::VecCall:
			p_vm.vector_call(scratch, in);
			break;
		case ir::Op::StrCall:
			p_vm.string_call(scratch, in, p_vm.stack_.data() + base);
			break;
		case ir::Op::RegExpCall:
			p_vm.regexp_call(scratch, in);
			break;
		case ir::Op::DateCall:
			p_vm.date_call(scratch, in);
			break;
		default: // VecNew and ArrayNew.
			p_vm.vector_op(scratch, in, p_vm.stack_.data() + base);
			break;
	}
	Slot made = p_vm.stack_[base + first + count];
	for (size_t i = 0; i < count; i++) {
		p_vm.drop(p_kinds[i], p_vm.stack_[base + first + i]);
	}
	p_vm.stack_.resize(base + first);
	if (p_vm.failing_) {
		p_vm.drop(p_result, made);
		return std::nullopt;
	}
	return made;
}

// forEach, map, filter, some and every, as the lowerer writes them for a typed vector (lower_functions.cpp,
// vector_each): the length read once, each element read as it is reached, and only an answer of true
// counting for filter, some and every.
void LibraryCall::each() {
	const EachMethod which = static_cast<EachMethod>(method_.which);
	Vector *vector = static_cast<Vector *>(vm_.held_object(holder_));
	const uint32_t klass_index = vector->klass;
	const ir::Class &klass = vm_.module_.classes[klass_index];
	const Slot callback = given_[0];
	if (which != EachMethod::Map && given_.size() > 1) {
		vm_.callback_this(callback, given_[1]);
		if (vm_.failing_) {
			return;
		}
	}
	const uint32_t limit = vector->length;
	const bool collects = which == EachMethod::Map || which == EachMethod::Filter;
	Vector *made = collects ? vm_.fresh_vector(klass_index, which == EachMethod::Map && !klass.array ? limit : 0) : nullptr;
	if (collects && !made) {
		return;
	}
	bool answer_all = which == EachMethod::Every;
	bool ok = true;
	Object *function = any::to_boolean(callback) ? vm_.function_in(Kind::Any, callback) : nullptr;
	ok = !vm_.failing_;
	for (uint32_t at = 0; ok && function && at < limit; at++) {
		if (at >= vector->length && !klass.array) {
			vm_.raise_range(at, vector->length);
			ok = false;
			break;
		}
		// The element as it is now: what the function does to the vector does not change what filter keeps.
		Slot element = at < vector->length ? vm_.share(klass.element, klass.element == Kind::Any ? any::filled(vector->data[at]) : vector->data[at]) : any::undefined();
		const Slot item = vm_.to_any(klass.element, element);
		const Slot arguments[3] = { item, any::integer(static_cast<int32_t>(at)), holder_ };
		const std::optional<Slot> answer = vm_.call_function_within(function, arguments, 3);
		ok = answer.has_value();
		const bool yes = ok && any::tag(*answer) == AnyTag::Boolean && any::to_bool_payload(*answer);
		bool settled = false;
		if (ok && which == EachMethod::Map && klass.array) {
			ok = vm_.resize_vector(made, made->length + 1);
			if (ok) {
				made->data[made->length - 1] = vm_.share(Kind::Any, *answer);
			}
		} else if (ok && which == EachMethod::Map) {
			Slot value;
			value.bits = 0;
			ok = vm_.from_any(*answer, klass.element, klass.element_check, &value);
			if (ok && at < made->length) {
				vm_.put(klass.element, made->data[at], value);
			} else if (ok) {
				vm_.drop(klass.element, value);
			}
		} else if (ok && which == EachMethod::Filter && yes) {
			ok = vm_.resize_vector(made, made->length + 1);
			if (ok) {
				made->data[made->length - 1] = vm_.share(klass.element, element);
			}
		} else if (ok && (which == EachMethod::Some || which == EachMethod::Every) && yes == (which == EachMethod::Some)) {
			answer_all = which == EachMethod::Some;
			settled = true;
		}
		if (answer) {
			vm_.drop_any(*answer);
		}
		vm_.drop_any(item);
		vm_.drop(klass.element, element);
		if (settled) {
			break;
		}
	}
	if (!ok) {
		if (made) {
			vm_.release_object(made);
		}
	} else if (collects) {
		give(Kind::Obj, of_object(made));
	} else if (which != EachMethod::ForEach) {
		give(Kind::Bool, of_bool(answer_all));
	}
}

// A Function's call(thisArg, ...) and apply(thisArg, array). thisArg is set aside: a method keeps its own
// object, and a function expression can't use `this`. The call is left for the loop to carry on with.
void LibraryCall::invoke() {
	Object *function = vm_.held_object(holder_);
	const uint32_t target = static_cast<uint32_t>(function->fields()[ir::FUNCTION_INDEX].i);
	Object *first = static_cast<Object *>(function->fields()[ir::FUNCTION_FIRST].p);
	std::vector<Slot> arguments;
	if (static_cast<ir::InvokeMethod>(method_.which) == ir::InvokeMethod::Call) {
		for (size_t i = 1; i < given_.size(); i++) {
			arguments.push_back(vm_.share(Kind::Any, given_[i]));
		}
	} else if (given_.size() > 1 && any::tag(given_[1]) != AnyTag::Null && any::tag(given_[1]) != AnyTag::Undefined) {
		const Object *list = vm_.held_object(given_[1]);
		if (!list || !vm_.module_.classes[list->klass].array) {
			vm_.raise(vm_.module_.type_error_class, "Error #1116: second argument to Function.prototype.apply must be an array.");
			return;
		}
		const Vector *array = static_cast<const Vector *>(list);
		for (uint32_t i = 0; i < array->length; i++) {
			arguments.push_back(vm_.share(Kind::Any, any::filled(array->data[i])));
		}
	}
	vm_.call_with(target, first, arguments, out_);
	for (Slot each : arguments) {
		vm_.drop_any(each);
	}
}

} // namespace vm
} // namespace kas3
