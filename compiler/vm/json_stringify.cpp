// JSON.stringify (15.12.3): Str, JO and JA, written into one text as they go.
#include "json_work.h"

#include "number_text.h"
#include "table.h"
#include "text_units.h"

#include <cmath>

namespace kas3 {
namespace vm {

using ir::Kind;

namespace {

// avmplus's words, from recollection; ECMAScript names no message, and passes over a replacer of
// another kind where Flash raises.
constexpr std::string_view CYCLE = "Error #1129: Cyclic structure cannot be converted to JSON string.";
constexpr std::string_view BAD_REPLACER = "Error #1131: Replacer argument to JSON stringifier must be an array or a two parameter function.";
constexpr std::string_view ENGINE_VALUE = "JSON.stringify can't write one of Godot's values or objects; convert it to an AS3 value first";

bool is_number(AnyTag p_tag) {
	return p_tag == AnyTag::Int || p_tag == AnyTag::Number || p_tag == AnyTag::Wide;
}

} // namespace

std::optional<Str *> Json::stringify(Slot p_value, Slot p_replacer, Slot p_space) {
	if (!options(p_replacer, p_space)) {
		return std::nullopt;
	}
	const Wrote wrote = write(vm_.share(Kind::Any, p_value), "");
	if (wrote == Wrote::Failed) {
		return std::nullopt;
	}
	if (wrote == Wrote::Nothing) {
		return nullptr;
	}
	Str *made = new_string(out_);
	if (!made) {
		vm_.out_of_memory();
		return std::nullopt;
	}
	return made;
}

// The replacer, a function or an Array of the keys to write; and the gap, from space: up to ten spaces,
// or the first ten units of a String.
bool Json::options(Slot p_replacer, Slot p_space) {
	Object *replacer = vm_.held_object(p_replacer);
	const AnyTag given = any::tag(p_replacer);
	if (replacer && replacer->klass == vm_.module_.function_class) {
		replacer_ = replacer;
	} else if (replacer && vm_.module_.classes[replacer->klass].array) {
		listed_ = true;
		const Vector *keys = static_cast<const Vector *>(replacer);
		for (uint32_t i = 0; i < keys->length; i++) {
			const Slot each = slots_of(keys)[i];
			const std::optional<std::string> name = any::tag(each) == AnyTag::String || is_number(any::tag(each)) ? any::text(each) : std::nullopt;
			bool seen = false;
			for (const std::string &kept : keys_) {
				seen = seen || (name && kept == *name);
			}
			if (name && !seen) {
				keys_.push_back(*name);
			}
		}
	} else if (given != AnyTag::Null && given != AnyTag::Undefined) {
		vm_.raise(vm_.module_.type_error_class, std::string(BAD_REPLACER));
		return false;
	}
	if (is_number(any::tag(p_space))) {
		const double count = std::fmin(10, std::trunc(any::to_number(p_space)));
		gap_.assign(count >= 1 ? static_cast<size_t>(count) : 0, ' ');
	} else if (any::tag(p_space) == AnyTag::String) {
		const std::string_view text = static_cast<const Str *>(any::pointer(p_space))->view();
		const int32_t length = utf16_length(text);
		gap_ = units_between(text, 0, static_cast<uint32_t>(length < 10 ? length : 10));
	}
	return true;
}

// Str(key, holder), from the value got: its toJSON(key), then the replacer, then the value as text.
Json::Wrote Json::write(Slot p_owned, std::string_view p_key) {
	if (!to_json(&p_owned, p_key)) {
		vm_.drop_any(p_owned);
		return Wrote::Failed;
	}
	if (replacer_) {
		Slot replaced;
		const bool called = call(replacer_, p_key, p_owned, true, &replaced);
		vm_.drop_any(p_owned);
		if (!called) {
			return Wrote::Failed;
		}
		p_owned = replaced;
	}
	const Wrote wrote = value(p_owned);
	vm_.drop_any(p_owned);
	return wrote;
}

Json::Wrote Json::value(Slot p_value) {
	// A freed Godot object is null, and JSON writes null.
	switch (any::tag(vm_.present_or_null(p_value))) {
		case AnyTag::Undefined:
			return Wrote::Nothing;
		case AnyTag::Null:
			out_ += "null";
			return Wrote::Value;
		case AnyTag::Boolean:
			out_ += any::to_bool_payload(p_value) ? "true" : "false";
			return Wrote::Value;
		case AnyTag::Int:
			out_ += std::to_string(any::to_int_payload(p_value));
			return Wrote::Value;
		case AnyTag::Number: {
			const double number = any::to_number_payload(p_value);
			out_ += std::isfinite(number) ? number_to_string(number) : "null";
			return Wrote::Value;
		}
		case AnyTag::Wide:
			out_ += wide_radix_text(static_cast<const Wide *>(any::pointer(p_value))->value, 10);
			return Wrote::Value;
		case AnyTag::String:
			json::quote(static_cast<const Str *>(any::pointer(p_value))->view(), &out_);
			return Wrote::Value;
		case AnyTag::Object: {
			Object *object = static_cast<Object *>(any::pointer(p_value));
			return object->klass == vm_.module_.function_class ? Wrote::Nothing : container(object);
		}
		default:
			vm_.raise(vm_.module_.type_error_class, std::string(ENGINE_VALUE));
			return Wrote::Failed;
	}
}

// An Object, an Array, a Vector or an object of a class of the program, which the caller holds.
Json::Wrote Json::container(Object *p_object) {
	for (const Object *open : open_) {
		if (open == p_object) {
			vm_.raise(vm_.module_.type_error_class, std::string(CYCLE));
			return Wrote::Failed;
		}
	}
	if (open_.size() == json::MAX_DEPTH) {
		vm_.stack_overflow();
		return Wrote::Failed;
	}
	open_.push_back(p_object);
	indent_ += gap_;
	const ir::Class &klass = vm_.module_.classes[p_object->klass];
	const Wrote wrote = klass.element != Kind::Void ? array(static_cast<Vector *>(p_object)) : object(p_object);
	indent_.resize(indent_.size() - gap_.size());
	open_.pop_back();
	return wrote;
}

// One member: what goes before it, its name in an object, and its value. In an object a value that is
// nothing takes the member out again; in an Array it is null.
Json::Wrote Json::member(bool p_first, std::string_view p_key, bool p_named, Slot p_owned) {
	const size_t mark = out_.size();
	if (!p_first) {
		out_ += ',';
	}
	if (!gap_.empty()) {
		out_ += '\n';
		out_ += indent_;
	}
	if (p_named) {
		json::quote(p_key, &out_);
		out_ += gap_.empty() ? ":" : ": ";
	}
	const Wrote wrote = write(p_owned, p_key);
	if (wrote == Wrote::Nothing && p_named) {
		out_.resize(mark);
	} else if (wrote == Wrote::Nothing) {
		out_ += "null";
	}
	return wrote;
}

void Json::close(bool p_any, char p_bracket) {
	if (p_any && !gap_.empty()) {
		out_ += '\n';
		out_.append(indent_, 0, indent_.size() - gap_.size());
	}
	out_ += p_bracket;
}

// JA: every element up to the length it had at the start; one that is gone since is undefined.
Json::Wrote Json::array(Vector *p_vector) {
	const ir::Class &klass = vm_.module_.classes[p_vector->klass];
	const uint32_t length = p_vector->length;
	out_ += '[';
	for (uint32_t i = 0; i < length; i++) {
		// A hole is undefined to a replacer, as to anything that reads it.
		Slot each = any::undefined();
		if (i < p_vector->length && !element_to_any(vm_.library_, klass, place(p_vector, i), &each)) {
			return Wrote::Failed;
		}
		if (vm_.failing_ || member(i == 0, std::to_string(i), false, each) == Wrote::Failed) {
			return Wrote::Failed;
		}
	}
	close(length > 0, ']');
	return Wrote::Value;
}

// JO: the replacer's keys, or the object's own: an Object's in order, a class's public fields (its base's
// first) and then its public getters. A class of the library (an Error) has none of its own to give.
std::vector<std::string> Json::names_of(Object *p_object) const {
	if (listed_) {
		return keys_;
	}
	std::vector<std::string> names;
	const ir::Class &klass = vm_.module_.classes[p_object->klass];
	if (klass.table) {
		for (const Entry &entry : entries_of(p_object)->all()) {
			if (any::tag(entry.key) == AnyTag::String && !entry.hidden) {
				names.emplace_back(static_cast<const Str *>(any::pointer(entry.key))->view());
			}
		}
		return names;
	}
	size_t fields = 0, methods = 0;
	for (uint32_t at = p_object->klass; at != ir::NONE; at = vm_.module_.classes[at].base) {
		if (vm_.module_.classes[at].built_in) {
			fields = vm_.module_.classes[at].fields.size();
			methods = vm_.module_.classes[at].named_methods.size();
			break;
		}
	}
	for (size_t i = fields; i < klass.fields.size(); i++) {
		if (klass.fields[i].is_public) {
			names.push_back(klass.fields[i].name);
		}
	}
	for (size_t i = methods; i < klass.named_methods.size(); i++) {
		if (klass.named_methods[i].first.compare(0, 4, "get ") == 0) {
			names.push_back(klass.named_methods[i].first.substr(4));
		}
	}
	return names;
}

// The member's value with a reference of its own; undefined for one the object does not have. False if
// a getter raised.
bool Json::member_value(Object *p_object, const std::string &p_name, Slot *r_value) {
	*r_value = any::undefined();
	const ir::Class &klass = vm_.module_.classes[p_object->klass];
	Interpreter::PropertyKey key;
	key.owned_text = p_name;
	if (klass.table) {
		if (const std::optional<uint32_t> at = vm_.entry_of(entries_of(p_object), key)) {
			*r_value = vm_.share(Kind::Any, entries_of(p_object)->value_at(*at));
		}
		return true;
	}
	if (const std::optional<uint32_t> field = klass.field(p_name, true)) {
		*r_value = vm_.to_any(klass.fields[*field].kind, vm::field(klass, p_object, *field));
		return !vm_.failing_;
	}
	if (vm_.named_method(klass, p_name, "get ")) {
		return vm_.get_property(any::object(p_object), key, r_value);
	}
	return true;
}

Json::Wrote Json::object(Object *p_object) {
	const std::vector<std::string> names = names_of(p_object);
	out_ += '{';
	bool any = false;
	for (const std::string &name : names) {
		Slot each;
		if (!member_value(p_object, name, &each)) {
			return Wrote::Failed;
		}
		const Wrote wrote = member(!any, name, true, each);
		if (wrote == Wrote::Failed) {
			return Wrote::Failed;
		}
		any = any || wrote == Wrote::Value;
	}
	close(any, '}');
	return Wrote::Value;
}

} // namespace vm
} // namespace kas3
