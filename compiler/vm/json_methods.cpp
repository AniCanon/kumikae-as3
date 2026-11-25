// JsonCall, and JSON.parse: the text read into Objects and Arrays, then revived (15.12.2).
#include "json_work.h"

#include "table.h"

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::JsonFunction;
using ir::Kind;
using ir::NONE;

namespace {

// avmplus's words, from recollection; ECMAScript names no message.
constexpr std::string_view PARSE_ERROR = "Error #1132: Invalid JSON parse input.";

} // namespace

void Interpreter::json_call(const Function &p_function, const Instruction &p_in) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	std::vector<Slot> given; // Held while the program may run.
	for (uint32_t reg : list) {
		given.push_back(to_any(p_function.registers[reg], stack_[frames_.back().base + reg]));
	}
	Json json(*this);
	const auto result = [&](Kind p_kind, Slot p_owned) { put(p_kind, stack_[frames_.back().base + p_in.a], p_owned); };
	switch (static_cast<JsonFunction>(p_in.b)) {
		case JsonFunction::Parse:
			if (std::optional<Slot> parsed = json.parse(given[0], held_object(given[1]))) {
				result(Kind::Any, any::tag(*parsed) == AnyTag::Undefined ? any::null() : *parsed); // As the declared Object holds it.
			}
			break;
		case JsonFunction::Stringify:
			if (const std::optional<Str *> written = json.stringify(given[0], given[1], given[2])) {
				Slot out;
				out.bits = 0;
				out.p = *written;
				result(Kind::Str, out);
			}
			break;
		case JsonFunction::ToJson: {
			Slot value = share(Kind::Any, given[0]);
			const Str *key = static_cast<const Str *>(any::pointer(given[1]));
			if (json.to_json(&value, key ? key->view() : std::string_view("null"))) {
				result(Kind::Any, value);
			}
			break;
		}
	}
	for (Slot each : given) {
		drop_any(each);
	}
}

std::optional<Slot> Json::parse(Slot p_text, Object *p_reviver) {
	const uint32_t syntax = vm_.module_.syntax_error_class != NONE ? vm_.module_.syntax_error_class : vm_.module_.error_class;
	if (any::tag(p_text) != AnyTag::String) {
		vm_.raise(syntax, std::string(PARSE_ERROR));
		return std::nullopt;
	}
	const json::Fault fault = json::parse(static_cast<const Str *>(any::pointer(p_text))->view(), *this);
	if (fault != json::Fault::None) {
		abandon();
		if (fault == json::Fault::Syntax) {
			vm_.raise(syntax, std::string(PARSE_ERROR));
		} else if (fault == json::Fault::TooDeep) {
			vm_.stack_overflow();
		}
		return std::nullopt; // Stopped: the builder raised.
	}
	const Slot root = root_;
	root_ = any::undefined();
	if (!p_reviver) {
		return root;
	}
	reviver_ = p_reviver;
	return walk(root, "", 0);
}

// Lets go of what a reading that failed had made.
void Json::abandon() {
	for (const Open &open : building_) {
		vm_.drop_any(any::object(open.container));
	}
	building_.clear();
	vm_.drop_any(root_);
	root_ = any::undefined();
}

// A value read: the root, the next element of the Array being read, or the value of the key just read.
bool Json::add(Slot p_owned) {
	if (building_.empty()) {
		root_ = p_owned;
		return true;
	}
	Open &open = building_.back();
	if (vm_.module_.classes[open.container->klass].array) {
		Vector *array = static_cast<Vector *>(open.container);
		const uint32_t room = array->capacity < 8 ? 8 : array->capacity * 2;
		if (array->length == array->capacity &&
				(array->length >= MAX_VECTOR_LENGTH || !vector_reserve(array, room < MAX_VECTOR_LENGTH ? room : MAX_VECTOR_LENGTH))) {
			vm_.drop_any(p_owned);
			vm_.out_of_memory();
			return false;
		}
		slots_of(array)[array->length++] = p_owned;
		return true;
	}
	Interpreter::PropertyKey key;
	key.owned_text = open.key;
	const bool set = vm_.set_entry(entries_of(open.container), key, p_owned);
	vm_.drop_any(p_owned);
	return set;
}

bool Json::null() {
	return add(any::null());
}

bool Json::boolean(bool p_value) {
	return add(any::boolean(p_value));
}

bool Json::number(double p_value) {
	const Slot made = vm_.any_number(p_value);
	return !vm_.failing_ && add(made);
}

bool Json::string(std::string_view p_text) {
	Str *made = new_string(p_text);
	if (!made) {
		vm_.out_of_memory();
		return false;
	}
	return add(any::string(made));
}

bool Json::begin_object() {
	const uint32_t klass = vm_.module_.object_class;
	Object *made = new_table(klass, vm_.heap_.list_for(klass));
	if (!made) {
		vm_.out_of_memory();
		return false;
	}
	building_.push_back({ made, std::string() });
	return true;
}

bool Json::key(std::string_view p_text) {
	building_.back().key.assign(p_text);
	return true;
}

bool Json::end_object() {
	Object *done = building_.back().container;
	building_.pop_back();
	return add(any::object(done));
}

bool Json::begin_array() {
	Vector *made = vm_.fresh_vector(vm_.module_.array_class, 0);
	if (!made) {
		return false;
	}
	building_.push_back({ made, std::string() });
	return true;
}

bool Json::end_array() {
	return end_object();
}

// Walk (15.12.2): an Object's or an Array's members revived first, each in turn, then the value itself
// given to the reviver. A member revived to undefined is deleted.
std::optional<Slot> Json::walk(Slot p_owned, std::string_view p_name, size_t p_depth) {
	Object *object = vm_.held_object(p_owned);
	const ir::Class *klass = object ? &vm_.module_.classes[object->klass] : nullptr;
	if (klass && (klass->array || klass->table)) {
		if (p_depth == json::MAX_DEPTH) {
			vm_.drop_any(p_owned);
			vm_.stack_overflow();
			return std::nullopt;
		}
		std::vector<Interpreter::PropertyKey> keys;
		if (klass->array) {
			for (uint32_t i = 0; i < static_cast<Vector *>(object)->length; i++) {
				keys.emplace_back();
				keys.back().index = i;
				keys.back().owned_text = std::to_string(i);
			}
		} else {
			for (const Entry &entry : entries_of(object)->all()) {
				if (any::tag(entry.key) == AnyTag::String && !entry.hidden) {
					keys.emplace_back();
					keys.back().owned_text = static_cast<const Str *>(any::pointer(entry.key))->view();
				}
			}
		}
		for (const Interpreter::PropertyKey &key : keys) {
			Slot member = any::undefined();
			if (klass->array) {
				Vector *array = static_cast<Vector *>(object);
				member = *key.index < array->length ? vm_.share(Kind::Any, any::filled(slots_of(array)[*key.index])) : any::undefined();
			} else if (const std::optional<uint32_t> at = vm_.entry_of(entries_of(object), key)) {
				member = vm_.share(Kind::Any, entries_of(object)->value_at(*at));
			}
			const std::optional<Slot> revived = walk(member, key.name(), p_depth + 1);
			bool done = revived.has_value();
			bool deleted = false;
			if (done && any::tag(*revived) == AnyTag::Undefined) {
				done = vm_.delete_property(p_owned, key, &deleted);
			} else if (done) {
				done = vm_.set_property(p_owned, key, *revived);
				vm_.drop_any(*revived);
			}
			if (!done) {
				vm_.drop_any(p_owned);
				return std::nullopt;
			}
		}
	}
	Slot revived;
	const bool called = call(reviver_, p_name, p_owned, true, &revived);
	vm_.drop_any(p_owned);
	return called ? std::optional<Slot>(revived) : std::nullopt;
}

// A function given the key, and with p_two the value too (borrowed): what it gives, with a reference of its own.
bool Json::call(Object *p_function, std::string_view p_key, Slot p_value, bool p_two, Slot *r_value) {
	Str *key = new_string(p_key);
	if (!key) {
		vm_.out_of_memory();
		return false;
	}
	const Slot given[2] = { any::string(key), p_value };
	const std::optional<Slot> result = vm_.call_function_within(p_function, given, p_two ? 2 : 1);
	vm_.drop_any(given[0]);
	if (result) {
		*r_value = *result;
	}
	return result.has_value();
}

bool Json::to_json(Slot *r_owned, std::string_view p_key) {
	Object *object = vm_.held_object(*r_owned);
	const ir::Class *klass = object ? &vm_.module_.classes[object->klass] : nullptr;
	if (!klass || object->klass == vm_.module_.function_class) {
		return true;
	}
	const auto replace = [&](Slot p_owned) {
		vm_.drop_any(*r_owned);
		*r_owned = p_owned;
	};
	if (object->klass == vm_.module_.date_class) {
		Str *made = vm_.text(vm_.date_text(object, ir::DateText::Full)); // As the Date's own toJSON() gives it.
		if (made) {
			replace(any::string(made));
		}
		return made != nullptr;
	}
	Interpreter::PropertyKey name;
	name.owned_text = "toJSON";
	Slot found = any::undefined();
	Entries *entries = klass->table ? entries_of(object) : klass->array ? static_cast<Vector *>(object)->properties : nullptr;
	if (const std::optional<uint32_t> at = vm_.entry_of(entries, name)) {
		found = vm_.share(Kind::Any, entries->value_at(*at));
	} else if (klass->dictionary) {
		Str *made = vm_.text("Dictionary");
		if (made) {
			replace(any::string(made));
		}
		return made != nullptr;
	} else if (klass->element == Kind::Void && !klass->table) {
		if (const std::optional<uint32_t> slot = vm_.named_method(*klass, name.name())) {
			Object *made = vm_.make_function(klass->methods[*slot], object);
			if (!made) {
				return false;
			}
			found = any::object(made);
		} else if (const std::optional<uint32_t> field = klass->field(name.name(), true)) {
			found = vm_.to_any(klass->fields[*field].kind, vm::field(*klass, object, *field));
		}
	}
	Object *function = vm_.held_object(found);
	bool kept = true;
	if (function && function->klass == vm_.module_.function_class) {
		Slot made;
		kept = call(function, p_key, any::undefined(), false, &made);
		if (kept) {
			replace(made);
		}
	}
	vm_.drop_any(found);
	return kept;
}

} // namespace vm
} // namespace kas3
