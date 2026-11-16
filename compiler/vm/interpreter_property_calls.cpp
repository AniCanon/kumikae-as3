// Objects and Dictionaries made, a property called by its name, and the loops over properties.
#include "interpreter_internal.h"

#include "table.h"

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;
using ir::NONE;
using ir::Op;

namespace {

Slot of_pointer(void *p_value) {
	Slot slot;
	slot.bits = 0;
	slot.p = p_value;
	return slot;
}

} // namespace

// The operations on properties by name, from the loop: one call there, out of the loop's way.
void Interpreter::dynamic_op(const Function &p_function, const Instruction &p_in) {
	switch (p_in.op) {
		case Op::TableNew:
			table_new(p_function, p_in);
			break;
		case Op::DynCall:
			property_call(p_function, p_in);
			break;
		case Op::DynKeys:
		case Op::DynNext:
		case Op::DynKey:
			property_loop(p_function, p_in);
			break;
		default:
			property_op(p_function, p_in);
			break;
	}
}

// TableNew: an empty Object or Dictionary. Weak keys would need the collector to forget a key nothing else
// holds, which reference counting can't tell soundly: a Dictionary asked for them is refused as it runs.
void Interpreter::table_new(const Function &p_function, const Instruction &p_in) {
	Slot *r = stack_.data() + frames_.back().base;
	(void)p_function;
	if (p_in.c != NONE && r[p_in.c].b) {
		raise_unsupported("a Dictionary with weak keys");
		return;
	}
	Object *made = new_table(p_in.b, collector_.list_for(p_in.b));
	if (!made) {
		out_of_memory();
		return;
	}
	assign(Kind::Obj, r[p_in.a], made);
}

// Whether a value is one of Godot's Dictionaries and the class is flash.utils.Dictionary: AS3 takes one as
// a Dictionary of its own, as it takes one of Godot's Arrays as an Array.
bool Interpreter::engine_dictionary(Slot p_value, uint32_t p_class) const {
	return any::tag(p_value) == AnyTag::Value && p_class < module_.classes.size() && module_.classes[p_class].dictionary &&
			host_.handles.is_class(any::pointer(p_value), "Dictionary");
}

Object *Interpreter::copy_engine_dictionary(void *p_dictionary, uint32_t p_class) {
	Object *made = table_of_engine(p_dictionary, p_class);
	if (!made) {
		report();
	}
	return made;
}

// A new Dictionary of the keys and values of one of Godot's, in its order: a copy. A key that is a number
// or a Boolean is its text, so two of Godot's keys (1 and "1") may become one: the later value is kept. A
// key that is one of Godot's objects is refused (its handle is no identity). Null, with the error raised,
// if the engine could not give them.
Object *Interpreter::table_of_engine(void *p_dictionary, uint32_t p_class) {
	std::string error;
	Wanted wanted;
	wanted.kind = Kind::Val;
	Slot keys;
	keys.bits = 0;
	if (!host_.handles.call(p_dictionary, "keys", nullptr, 0, wanted, &keys, &error)) {
		raise(module_.error_class, error);
		return nullptr;
	}
	Wanted whole;
	whole.kind = Kind::I64;
	Slot size;
	size.bits = 0;
	Object *made = keys.p && host_.handles.call(keys.p, "size", nullptr, 0, whole, &size, &error) ? new_table(p_class, collector_.list_for(p_class)) : nullptr;
	bool ok = made != nullptr;
	if (!ok && error.empty()) {
		out_of_memory();
	} else if (!ok) {
		raise(module_.error_class, error);
	}
	Wanted untyped;
	untyped.kind = Kind::Any;
	const Slot holder = made ? any::object(made) : any::undefined();
	for (int64_t i = 0; ok && i < size.l; i++) {
		Argument at;
		at.kind = Kind::I64;
		at.slot.bits = 0;
		at.slot.l = i;
		Slot key = any::undefined(), value = any::undefined();
		ok = host_.handles.index_get(keys.p, at, untyped, &key, &error) && host_.handles.index_get(p_dictionary, Argument{ Kind::Any, key }, untyped, &value, &error);
		if (!ok) {
			raise(module_.error_class, error);
		}
		PropertyKey found;
		ok = ok && property_key(holder, key, &found) && set_entry(entries_of(made), found, value);
		drop_any(key);
		drop_any(value);
	}
	if (keys.p) {
		host_.handles.drop(keys.p);
	}
	if (!ok && made) {
		release_object(made);
		made = nullptr;
	}
	return made;
}

// The Object's own toString, when one of its properties is a function of that name: what String(o) and
// trace(o) say. *r_own is false when it has none, and Object's own words are wanted.
bool Interpreter::table_text(Object *p_table, std::string *r_text, bool *r_own) {
	const std::optional<uint32_t> at = entries_of(p_table)->find_text("toString");
	const Slot value = at ? entries_of(p_table)->value_at(*at) : any::undefined();
	Object *function = held_object(value);
	*r_own = function && function->klass == module_.function_class;
	if (!*r_own) {
		return true;
	}
	retain(function);
	const std::optional<Slot> said = call_function_within(function, nullptr, 0);
	release_object(function);
	if (!said) {
		return false;
	}
	const bool done = any_text(*said, r_text);
	drop_any(*said);
	return done;
}

// Pushes a call of the function with what it takes first, the result to go to the caller's register.
void Interpreter::call_with(uint32_t p_target, Object *p_first, const std::vector<Slot> &p_given, uint32_t p_out) {
	SpareSlots values(spare_slots_);
	if (function_arguments(p_target, p_first, p_given.size(), p_given.data(), &*values)) {
		push(p_target, values->data(), values->size(), p_out); // It takes references of its own.
		let_go_arguments(module_.functions[p_target], &*values);
	}
}

// Object's own methods, called on a value that has no property of that name: true if the name is one.
bool Interpreter::object_method(Slot p_holder, const PropertyKey &p_key, const std::vector<Slot> &p_given, uint32_t p_out) {
	const size_t base = frames_.back().base;
	const auto give = [&](Kind p_kind, Slot p_value) {
		if (p_out != NONE) {
			put(Kind::Any, stack_[base + p_out], to_any(p_kind, p_value));
		}
		drop(p_kind, p_value);
	};
	const std::string &name = p_key.text;
	if (name == "hasOwnProperty" || name == "propertyIsEnumerable") {
		PropertyKey asked;
		bool has = false;
		if (property_key(p_holder, p_given.empty() ? any::undefined() : p_given[0], &asked) && has_property(p_holder, asked, false, &has)) {
			const Object *object = held_object(p_holder);
			const bool listed = object && (module_.classes[object->klass].table || module_.classes[object->klass].array);
			Slot answer;
			answer.bits = 0;
			answer.b = has && (name == "hasOwnProperty" || listed);
			give(Kind::Bool, answer);
		}
		return true;
	}
	const AnyTag tag = any::tag(p_holder);
	if (name == "toString" && !p_given.empty() && (tag == AnyTag::Int || tag == AnyTag::Number || tag == AnyTag::Wide)) {
		raise_unsupported("calling 'toString' with a radix on a number held untyped");
		return true;
	}
	if (name == "toString" || name == "toLocaleString") {
		std::string words;
		if (any_text(p_holder, &words)) {
			if (Str *made = text(words)) {
				give(Kind::Str, of_pointer(made));
			}
		}
		return true;
	}
	if (name == "valueOf") {
		give(Kind::Any, share(Kind::Any, p_holder));
		return true;
	}
	if (name == "isPrototypeOf") {
		give(Kind::Bool, Slot{});
		return true;
	}
	return false;
}

// holder.name(arguments): the property's value called, as a function. A sealed object's method is called
// on it; a name it lacks is #1069; a property that is not a function, AS3's TypeError #1006.
void Interpreter::call_named(Slot p_holder, const PropertyKey &p_key, const std::vector<Slot> &p_given, uint32_t p_out) {
	Object *object = held_object(p_holder);
	const ir::Class *klass = object ? &module_.classes[object->klass] : nullptr;
	if (object && library_call(object, p_key.text, p_given, p_out)) {
		return; // RegExp's or Date's, done or raised.
	}
	if (klass && klass->element == Kind::Void && !klass->table) {
		if (const std::optional<uint32_t> slot = named_method(*klass, p_key.text)) {
			call_with(klass->methods[*slot], object, p_given, p_out);
			return;
		}
	}
	Slot value = any::undefined();
	bool own = false;
	if (klass && (klass->table || klass->array || (klass->element == Kind::Void && klass->field(p_key.text, true)))) {
		if (!has_property(p_holder, p_key, false, &own)) {
			return;
		}
		if (own && !get_property(p_holder, p_key, &value)) {
			return;
		}
	}
	Object *function = held_object(value);
	if (function && function->klass == module_.function_class) {
		const uint32_t target = static_cast<uint32_t>(function->fields()[ir::FUNCTION_INDEX].i);
		call_with(target, static_cast<Object *>(function->fields()[ir::FUNCTION_FIRST].p), p_given, p_out);
	} else if (!own && object_method(p_holder, p_key, p_given, p_out)) {
		// Object's own method, on a value with no property of that name.
	} else if (own || (klass && klass->table)) {
		raise(module_.type_error_class, "Error #1006: " + p_key.text + " is not a function.");
	} else {
		// A method of its prototype (an Array's push) can't be called by name yet; reading anything else
		// raises for what is not there (#1069), and what is read is no function.
		bool inherited = false;
		Slot read;
		if (!has_property(p_holder, p_key, true, &inherited)) {
			// Raised.
		} else if (inherited) {
			raise_unsupported("calling '" + p_key.text + "' on a value of type '" + holder_name(p_holder) + "' by its name");
		} else if (get_property(p_holder, p_key, &read)) {
			drop_any(read);
			raise(module_.type_error_class, "Error #1006: " + p_key.text + " is not a function.");
		}
	}
	drop_any(value);
}

// DynCall, once the arguments are worked out: the holder's property called with them.
void Interpreter::property_call(const Function &p_function, const Instruction &p_in) {
	const size_t base = frames_.back().base;
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	if (p_in.a != NONE) {
		put(Kind::Any, stack_[base + p_in.a], any::undefined()); // A function that gives nothing gives undefined.
	}
	const Slot holder = to_any(p_function.registers[p_in.b], stack_[base + p_in.b]);
	const Slot key = to_any(p_function.registers[list[0]], stack_[base + list[0]]);
	std::vector<Slot> given;
	for (size_t i = 1; i < list.size(); i++) {
		given.push_back(share(Kind::Any, stack_[base + list[i]]));
	}
	PropertyKey found;
	if (!held_object(holder) && (any::tag(holder) == AnyTag::Null || any::tag(holder) == AnyTag::Undefined)) {
		any_null_check(holder);
	} else if (property_key(holder, key, &found)) {
		call_named(holder, found, given, p_in.a);
	}
	drop_any(holder);
	drop_any(key);
	for (Slot each : given) {
		drop_any(each);
	}
}

// The properties a loop goes through, besides an Array's or a Vector's elements: a table's, or an Array's
// others; nothing for any other value.
Entries *Interpreter::loop_entries(Slot p_holder) const {
	Object *object = held_object(p_holder);
	const ir::Class *klass = object ? &module_.classes[object->klass] : nullptr;
	if (klass && klass->table) {
		return entries_of(object);
	}
	return klass && klass->array ? static_cast<Vector *>(object)->properties : nullptr;
}

// DynKeys, DynNext and DynKey: for in and for each. The cursor counts an Array's or a Vector's elements up
// from 0 (the next to look at), then the keys taken when the loop began down from -1: one deleted since is
// passed over, and one added since is not reached. An Array's element that is undefined is passed over.
void Interpreter::property_loop(const Function &p_function, const Instruction &p_in) {
	Slot *r = stack_.data() + frames_.back().base;
	if (p_in.op == Op::DynKeys) {
		const Slot holder = to_any(p_function.registers[p_in.b], r[p_in.b]);
		if (any::is_engine(holder)) {
			drop_any(holder);
			raise_unsupported("'for in' and 'for each' over one of Godot's values held untyped");
			return;
		}
		Entries *entries = loop_entries(holder);
		Vector *keys = entries && entries->size() && module_.array_class != NONE ? fresh_vector(module_.array_class, entries->size()) : nullptr;
		uint32_t at = 0;
		for (const Entry &entry : keys ? entries->all() : std::vector<Entry>()) {
			if (any::tag(entry.key) != AnyTag::Undefined) {
				keys->data[at++] = share(Kind::Any, entry.key);
			}
		}
		drop_any(holder);
		put(Kind::Obj, r[p_in.a], of_pointer(keys));
		return;
	}
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const Slot holder = to_any(p_function.registers[list[0]], r[list[0]]); // Dropped below; nothing here runs the program.
	const Vector *keys = static_cast<const Vector *>(r[list[1]].p);
	Object *object = held_object(holder);
	const ir::Class *klass = object ? &module_.classes[object->klass] : nullptr;
	const Vector *elements = klass && klass->element != Kind::Void ? static_cast<const Vector *>(object) : nullptr;
	double cursor = r[p_in.b].d;
	if (p_in.op == Op::DynKey) {
		Slot key = any::undefined();
		if (cursor >= 1) {
			Str *made = text(number_to_string(cursor - 1));
			key = made ? any::string(made) : any::undefined();
		} else if (keys) {
			key = share(Kind::Any, keys->data[static_cast<uint32_t>(-cursor - 2)]);
		}
		put(Kind::Any, r[p_in.a], key);
		drop_any(holder);
		return;
	}
	bool more = false;
	for (; !more && elements && cursor >= 0 && cursor < elements->length; cursor++) {
		const uint32_t at = static_cast<uint32_t>(cursor);
		more = !klass->array || any::tag(elements->data[at]) != AnyTag::Undefined;
	}
	cursor = more ? cursor : cursor >= 0 ? -1 : cursor;
	const Entries *entries = loop_entries(holder);
	for (; !more && keys && entries && static_cast<uint32_t>(-cursor - 1) < keys->length; cursor--) {
		const Slot key = keys->data[static_cast<uint32_t>(-cursor - 1)];
		more = any::tag(key) == AnyTag::String ? entries->find_text(static_cast<const Str *>(any::pointer(key))->view()).has_value()
											   : entries->find_object(any::pointer(key)).has_value();
	}
	r[p_in.b].d = cursor;
	Slot answer;
	answer.bits = 0;
	answer.b = more;
	put(Kind::Bool, r[p_in.a], answer);
	drop_any(holder);
}

} // namespace vm
} // namespace kas3
