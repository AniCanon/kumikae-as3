// What one of Godot's values or objects has under an AS3 name (GodotHost::named), for a value a script
// holds untyped: `untyped.name`, `untyped.queueFree()`. The name is looked for in Godot's name table
// (godot/godot_names.txt), under the value's own class and then under each class it extends, as the
// checker finds a member of a typed value: nothing is worked out backward from the AS3 name. What is found
// is kept by class and name, so that a name is looked for once.
#include "godot_calls.h"
#include "handles.h"
#include "program.h"
#include "handles.h"

#if !defined(KAS3_RUNTIME_ONLY)
#include "builtins.h"
#include "godot_names.h"
#endif
#include "worked.h"
#include "any.h"

#if defined(KAS3_TEST_COUNTS)
#include "../tests/script_read_allocations.h"
#endif

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/script.hpp>

#include <algorithm>
#include <string>
#include <unordered_map>

using namespace godot;

namespace kas3 {

void GodotHost::new_program() {
	program_++;
	script_entry();
#if !defined(KAS3_RUNTIME_ONLY)
	for (auto &entry : class_members_) {
		entry.second.misses.clear();
	}
	for (auto &members : value_members_) {
		members.misses.clear();
	}
#endif
}

#if !defined(KAS3_RUNTIME_ONLY)
uintptr_t GodotHost::handle_class(void *p_receiver, void *p_raw) {
	auto &type_id = rt::head_of(p_receiver)->godot_class;
	if (!type_id) {
		StringName klass;
		if (!gdextension_interface::object_get_class_name(p_raw, gdextension_interface::library, klass._native_ptr())) {
			return 0;
		}
		type_id = class_identity(klass);
	}
	return type_id;
}

static std::optional<uint64_t> attached_script(void *p_raw) {
	static const auto get_script = [] {
		const auto entry = godot_entry("GodotObject.getScript");
		return entry ? method_bind("Object", "get_script", godot_words(godot_entry_at(*entry).value)[2]) : nullptr;
	}();
	if (!get_script) {
		return std::nullopt;
	}
	Variant script;
	gdextension_interface::object_method_bind_ptrcall(get_script, p_raw, nullptr, script._native_ptr());
	return script.get_type() == Variant::OBJECT ? gdextension_interface::variant_get_object_instance_id(script._native_ptr()) : 0;
}
#endif

std::optional<vm::NamedShape> GodotHost::shape_of(void *p_receiver, uintptr_t p_delegated) {
#if defined(KAS3_RUNTIME_ONLY)
	(void)p_receiver;
	(void)p_delegated;
	return {};
#else
	if (!p_receiver) {
		return std::nullopt;
	}
	const Variant &value = *static_cast<const Variant *>(p_receiver);
	const Variant::Type type = value.get_type();
	if (type != Variant::OBJECT) {
		// Calls on other value types may mutate their copy of the receiver.
		if (type >= Variant::VECTOR2 && type <= Variant::COLOR) {
			return vm::NamedShape{ static_cast<uintptr_t>(type) + 1 };
		}
		return std::nullopt;
	}
	void *raw = live_handle_object(p_receiver);
	if (!raw) {
		return {};
	}
	const uintptr_t type_id = handle_class(p_receiver, raw);
	if (!type_id) {
		return {};
	}
	if (vm::Object *instance = script_on_live_handle(raw, value)) {
		return vm::NamedShape{ type_id, uint64_t(instance->klass) + 1, instance, raw };
	}
	// This binding goes through Godot's named dispatch, so a different foreign script or a
	// detached script on the same engine class needs no separate query for the Script resource.
	if (p_delegated == type_id) {
		return vm::NamedShape{ type_id, 0, nullptr, raw, true };
	}
	const auto id = attached_script(raw);
	if (!id) {
		return std::nullopt;
	}
	return vm::NamedShape{ type_id, *id, nullptr, raw };
#endif
}

const vm::NamedMember &GodotHost::script_binding(std::string_view p_name, bool p_method) {
	auto at = script_members_.find(p_name);
	if (at == script_members_.end()) {
		at = script_members_.try_emplace(std::string(p_name)).first;
	}
	auto &member = at->second[p_method];
	if (!member.invoke) {
		auto binding = std::make_unique<Worked>();
		binding->what = p_method ? Worked::What::Bound : Worked::What::Getter;
		binding->name = at->first;
		binding->dynamic_name = StringName(to_godot(p_name));
		member = { p_method ? vm::NamedMember::What::Method : vm::NamedMember::What::Property,
			at->first, true, !p_method, binding.get(), nullptr, true };
		named_bindings_.push_back(std::move(binding));
	}
	return member;
}

static bool native_member(const StringName &p_class, const StringName &p_key) {
	auto *db = ClassDBSingleton::get_singleton();
	if (db->class_has_method(p_class, p_key) || db->class_has_signal(p_class, p_key) || db->class_has_integer_constant(p_class, p_key)) {
		return true;
	}
	const auto properties = db->class_get_property_list(p_class);
	for (int i = 0; i < properties.size(); ++i) {
		if (StringName(Dictionary(properties[i])["name"]) == p_key) {
			return true;
		}
	}
	return false;
}

// Godot may receive this literal after a script is detached. A raw engine spelling must
// never expose a native member that the AS3 declaration table spells differently.
bool GodotHost::delegates_script(void *p_receiver, std::string_view p_name) {
	Object *object = static_cast<const Variant *>(p_receiver)->get_validated_object();
	if (!object) {
		return false;
	}
	const StringName klass = object->get_class();
	if (!computed_name_.made || computed_class_ != klass || computed_name_.text != p_name) {
		computed_name_.text.assign(p_name);
		computed_name_.godot = StringName(to_godot(p_name));
		computed_name_.made = true;
		computed_class_ = klass;
		can_delegate_ = !native_member(klass, computed_name_.godot);
	}
	return can_delegate_;
}

#if defined(KAS3_RUNTIME_ONLY)

// A build that carries no table of Godot's names can't say: the script is told so.
const vm::NamedMember &GodotHost::named(void *, std::string_view, vm::NameSource) {
	static constexpr vm::NamedMember unknown{ vm::NamedMember::What::Unknown, {} };
	return unknown;
}

#else

namespace {

// AS3's name for each of Godot's classes whose name AS3 already uses (GodotObject for Object), by Godot's
// name: read once from the table, where a class's entry is its AS3 name and Godot's.
const std::unordered_map<std::string, std::string> &renamed() {
	static const std::unordered_map<std::string, std::string> all = [] {
		std::unordered_map<std::string, std::string> made;
		const std::string text = godot_names_text();
		for (size_t at = 0; at < text.size();) {
			const size_t end = std::min(text.find('\n', at), text.size());
			const std::string_view line = std::string_view(text).substr(at, end - at);
			const size_t equals = line.find('=');
			at = end + 1;
			if (equals == std::string_view::npos || line[0] == '@' || line.substr(0, equals).find('.') != std::string_view::npos) {
				continue;
			}
			if (line.substr(0, equals) != line.substr(equals + 1)) {
				made.emplace(std::string(line.substr(equals + 1)), std::string(line.substr(0, equals)));
			}
		}
		return made;
	}();
	return all;
}

// What the table says of a member, as a script may use it by name.
vm::NamedMember described(const std::vector<std::string_view> &words) {
	vm::NamedMember found;
	found.godot = words[0];
	if (words.size() == 1 && !words[0].empty() && words[0][0] == '@') {
		// The AS3 way of listening to a signal: one property, and the rest are methods.
		found.what = words[0] == "@numListeners" ? vm::NamedMember::What::Property : vm::NamedMember::What::Method;
		found.written = false;
	} else if (words.size() > 1 && words[1] == "method") {
		found.what = vm::NamedMember::What::Method;
	} else if (words.size() > 1 && words[1] == "property") {
		found.what = vm::NamedMember::What::Property;
		found.read = words.size() > 3 && words[3] != "-";
		found.written = words.size() > 5 && words[5] != "-";
	} else if (words.size() > 1 && (words[1] == "signal" || words[1] == "field")) {
		found.what = vm::NamedMember::What::Property;
		found.written = words[1] == "field";
	}
	return found;
}

} // namespace

const vm::NamedMember &GodotHost::named(void *p_receiver, std::string_view p_name, vm::NameSource p_source) {
	const Variant &receiver = *static_cast<const Variant *>(p_receiver);
	const Variant::Type type = receiver.get_type();
	StringName klass;
	const bool object = type == Variant::OBJECT;
	if (object) {
		// This is the interned class identity godot-cpp uses for instance binding (core/object.cpp).
		const auto id = gdextension_interface::variant_get_object_instance_id(receiver._native_ptr());
		const auto owner = gdextension_interface::object_get_instance_from_id(id);
		if (!owner || !gdextension_interface::object_get_class_name(owner, gdextension_interface::library, klass._native_ptr())) {
			return vm::MISSING_MEMBER;
		}
	}
	return named_on_class(type, klass, p_name, p_source);
}

const vm::NamedMember &GodotHost::named_on_class(Variant::Type p_type, const StringName &p_class, std::string_view p_name, vm::NameSource p_source) {
	Members &members = p_type == Variant::OBJECT ? class_members_[p_class] : value_members_[p_type];
	if (const auto known = members.known.find(p_name); known != members.known.end()) {
		return known->second;
	}
	return *members.misses.lookup(p_name, p_source, [&] { return resolve_member(members, p_class, p_type, p_name); });
}

const vm::NamedMember *GodotHost::resolve_member(Members &p_members, StringName p_class, Variant::Type p_type, std::string_view p_name) {
	const bool object = p_type == Variant::OBJECT;
	if (!object) {
		p_class = StringName(Variant::get_type_name(p_type));
	}
	while (p_class != StringName()) {
		const std::string godot = String(p_class).utf8().get_data();
		const auto as3 = renamed().find(godot);
		const auto id = godot_entry((as3 == renamed().end() ? godot : as3->second) + "." + std::string(p_name));
		if (id) {
			const GodotEntry entry = godot_entry_at(*id);
			const auto words = godot_words(entry.value);
			vm::NamedMember found = described(words);
			const auto keep_binding = [this](std::unique_ptr<Worked> p_binding) -> const vm::Prepared * {
				if (!p_binding) {
					return nullptr;
				}
				p_binding->by_name = true;
				p_binding->dynamic_name = name(p_binding->name);
				named_bindings_.push_back(std::move(p_binding));
				return named_bindings_.back().get();
			};
			if (found.what == vm::NamedMember::What::Method) {
				found.invoke = keep_binding(method_of(godot, words, p_type, false, std::nullopt));
			} else if (found.what == vm::NamedMember::What::Property) {
				const ir::Kind kind = object ? ir::Kind::Ref : ir::Kind::Val;
				if (found.read) {
					found.invoke = keep_binding(member_of(godot, words, false, kind));
				}
				if (object && found.written) {
					found.setter = keep_binding(member_of(godot, words, true, kind));
				}
			}
			if (found.what != vm::NamedMember::What::Missing) {
				// The same constant table the IR uses owns both names, across program reloads.
				return &p_members.known.emplace(entry.key.substr(entry.key.find('.') + 1), found).first->second;
			}
		}
		p_class = object ? ClassDBSingleton::get_singleton()->get_parent_class(p_class) : StringName();
	}
	return &vm::MISSING_MEMBER;
}

#endif

const vm::NamedMember &GodotHost::script_getter(void *p_receiver, std::string_view p_name) {
	return delegates_script(p_receiver, p_name) ? script_binding(p_name, false) : script_member(p_receiver, p_name);
}

#if !defined(KAS3_RUNTIME_ONLY)
// Repeated reads often follow the same sequence. Check the previous key's successor first.
GodotHost::ReadKey *GodotHost::find_read_key(uintptr_t p_type, std::string_view p_name) {
	auto &hint = read_keys_[last_read_key_].next;
	auto matches = [&](const ReadKey &p_key) { return p_key.type == p_type && p_key.text == p_name; };
	if (!matches(read_keys_[hint])) {
		const auto found = std::find_if(read_keys_.begin(), read_keys_.end(), matches);
		if (found == read_keys_.end()) {
			return nullptr;
		}
		hint = static_cast<uint8_t>(found - read_keys_.begin());
	}
	last_read_key_ = hint;
	auto &entry = read_keys_[hint];
	entry.used = ++read_clock_;
	return &entry;
}

const StringName *GodotHost::keep_read_key(uintptr_t p_type, std::string_view p_name, const StringName &p_key) {
	auto victim = std::min_element(read_keys_.begin(), read_keys_.end(), [](const ReadKey &a, const ReadKey &b) { return a.used < b.used; });
	*victim = { p_type, std::string(p_name), p_key, ++read_clock_ };
	read_keys_[last_read_key_].next = static_cast<uint8_t>(victim - read_keys_.begin());
	last_read_key_ = read_keys_[last_read_key_].next;
	return &victim->key;
}
#endif

// Read an engine property by its Godot spelling, or a foreign field by the script's spelling.
const StringName *GodotHost::computed_getter(void *p_receiver, std::string_view p_name, void **r_owner, std::optional<StringName> &r_nested) {

#if defined(KAS3_RUNTIME_ONLY)
	(void)p_receiver;
	(void)p_name;
	(void)r_owner;
	(void)r_nested;
	return nullptr;
#else
	if (!p_receiver) {
		return nullptr;
	}
	const auto type = rt::head_of(p_receiver)->godot_class;
	if (type) {
		if (const auto *entry = find_read_key(type, p_name)) {
			*r_owner = live_handle_object(p_receiver);
			const auto &value = *static_cast<const Variant *>(p_receiver);
			return *r_owner && !script_on_live_handle(*r_owner, value) ? &entry->key : nullptr;
		}
	}
	const auto &value = *static_cast<const Variant *>(p_receiver);
	if (value.get_type() != Variant::OBJECT) {
		return nullptr;
	}
	void *owner = live_handle_object(p_receiver);
	StringName klass;
	if (!owner || script_on_live_handle(owner, value) || !gdextension_interface::object_get_class_name(owner, gdextension_interface::library, klass._native_ptr())) {
		return nullptr;
	}
	const auto &member = named_on_class(Variant::OBJECT, klass, p_name, vm::NameSource::Computed);
	StringName key;
	if (member.what == vm::NamedMember::What::Property && member.read && member.invoke &&
			static_cast<const Worked *>(member.invoke)->what == Worked::What::Getter) {
		key = static_cast<const Worked *>(member.invoke)->dynamic_name;
	} else if (member.what == vm::NamedMember::What::Missing && attached_script(owner).value_or(0)) {
		key = StringName(to_godot(p_name));
		if (native_member(klass, key)) {
			return nullptr;
		}
	} else {
		return nullptr;
	}
	*r_owner = owner;
	// A reentrant getter can use a warmed name. A new key must leave the outer name alive.
	if (active_reads_) {
		return &r_nested.emplace(key);
	}
	const auto identity = class_identity(klass);
	rt::head_of(p_receiver)->godot_class = identity;
	return keep_read_key(identity, p_name, key);
#endif
}

vm::ScriptRead GodotHost::read_script(void *p_receiver, void *p_owner, std::string_view p_name, const vm::Prepared *p_binding,
		const vm::Wanted &p_result, vm::Slot *r_value, std::string *r_error) {
#if defined(KAS3_TEST_COUNTS)
	ScriptReadAllocations allocations(p_name.substr(0, 16) == "allocation_probe");
#endif
	std::optional<StringName> nested;
	const StringName *key = p_binding ? &static_cast<const Worked *>(p_binding)->dynamic_name : computed_getter(p_receiver, p_name, &p_owner, nested);
	if (!key) {
		return vm::ScriptRead::Declined;
	}
	GDExtensionBool valid = false;
	Variant value;
	++active_reads_;
	gdextension_interface::variant_get_named(static_cast<const Variant *>(p_receiver)->_native_ptr(), key->_native_ptr(), value._native_ptr(), &valid);
	--active_reads_;
	if (!valid) {
		const auto &declared = reflected_member(p_receiver, p_name, true);
		if (declared.what == vm::NamedMember::What::Method) {
			return vm::ScriptRead::Method;
		}
		if (declared.what == vm::NamedMember::What::Missing) {
			return vm::ScriptRead::Missing;
		}
		*r_error = "Godot has no '" + std::string(p_name) + "' on this value";
		return vm::ScriptRead::Failed;
	}
	const auto type = value.get_type();
	if (type == Variant::CALLABLE && live_handle_object(p_receiver) == p_owner &&
			gdextension_interface::object_has_script_method(p_owner, key->_native_ptr())) {
		return vm::ScriptRead::Method;
	}
	if (p_result.kind == ir::Kind::Any && p_result.type.empty() && r_value) {
		if (type >= Variant::VECTOR2 && type <= Variant::COLOR) {
			auto *held = static_cast<Variant *>(p_result.reuse);
			if (held) {
				const auto previous = held->get_type();
				if (previous >= Variant::VECTOR2 && previous <= Variant::COLOR) {
					*held = std::move(value);
					*r_value = vm::any::godot_value(held);
					return vm::ScriptRead::Value;
				}
			}
			*r_value = vm::any::godot_value(make_handle(std::move(value)));
			return vm::ScriptRead::Value;
		}
		if (auto result = untyped_from_variant(value, type)) {
			*r_value = *result;
			return vm::ScriptRead::Value;
		}
		*r_error = std::string(vm::OUT_OF_MEMORY);
		return vm::ScriptRead::Failed;
	}
	return take(p_result, value, p_name, r_value, r_error) ? vm::ScriptRead::Value : vm::ScriptRead::Failed;
}

std::optional<bool> GodotHost::run_script(const vm::NamedShape &p_shape, const vm::Prepared &p_bind, const vm::Argument *p_operands, size_t p_count,
		const vm::Wanted &p_result, vm::Slot *r_value, std::string *r_error) {
	const auto &binding = static_cast<const Worked &>(p_bind);
	Variant value;
	{
		GDExtensionCallError error{};
		if (p_count == 1) {
			gdextension_interface::object_call_script_method(p_shape.owner, binding.dynamic_name._native_ptr(), nullptr, 0, value._native_ptr(), &error);
		} else {
			Arguments args(p_operands + 1, p_count - 1);
			gdextension_interface::object_call_script_method(p_shape.owner, binding.dynamic_name._native_ptr(),
					reinterpret_cast<const GDExtensionConstVariantPtr *>(args.pointers()), p_count - 1, value._native_ptr(), &error);
		}
		if (error.error == GDEXTENSION_CALL_ERROR_INVALID_METHOD) {
			return std::nullopt;
		}
		if (error.error != GDEXTENSION_CALL_OK) {
			*r_error = describe(error, binding.name);
			return false;
		}
	}
	if (p_result.kind == ir::Kind::Any && !p_result.reuse && r_value) {
		if (auto result = untyped_from_variant(value)) {
			*r_value = *result;
			return true;
		}
		*r_error = std::string(vm::OUT_OF_MEMORY);
		return false;
	}
	return take(p_result, value, binding.name, r_value, r_error);
}

std::optional<bool> GodotHost::run_named(const vm::NamedShape &p_shape, const vm::Prepared &p_bind, const vm::Argument *p_operands, size_t p_count,
		const vm::Wanted &p_result, vm::Slot *r_value, std::string *r_error) {
	const Worked &worked = static_cast<const Worked &>(p_bind);
	if ((worked.what == Worked::What::Bound && worked.name == "reload") ||
			(worked.what == Worked::What::Setter && worked.name == "source_code")) {
		script_entry();
	}
	if (p_shape.delegate || (p_shape.script && !p_shape.instance)) {
		auto *receiver = static_cast<Variant *>(p_operands[0].slot.p);
		bool valid = false;
		if (worked.what == Worked::What::Getter) {
			Variant value = receiver->get_named(worked.dynamic_name, valid);
			if (valid) {
				if (auto *held = reusable_math(p_result, value)) {
					*held = std::move(value);
					*r_value = vm::any::godot_value(held);
					return true;
				}
				return take(p_result, value, worked.name, r_value, r_error);
			}
			*r_error = "Godot has no '" + worked.name + "' on this value";
		} else if (worked.what == Worked::What::Setter) {
			const vm::Slot given = p_operands[1].slot;
			if (vm::any::tag(given) == vm::AnyTag::Value) {
				receiver->set_named(worked.dynamic_name, *static_cast<const Variant *>(vm::any::pointer(given)), valid);
			} else {
				receiver->set_named(worked.dynamic_name, to_variant(ir::Kind::Any, given), valid);
			}
			if (valid) {
				return true;
			}
			*r_error = "Godot could not set '" + worked.name + "' on this value";
		} else {
			return std::nullopt;
		}
		return false;
	}
	if (p_shape.owner && worked.bound && p_count <= MOST_OPERANDS) {
		if (worked.setter_argument && p_count == 2 && vm::any::tag(p_operands[1].slot) == vm::AnyTag::Value) {
			auto *given = static_cast<Variant *>(vm::any::pointer(p_operands[1].slot));
			if (given->get_type() == worked.parameters.front()) {
				const void *arg = worked.setter_argument(given->_native_ptr());
				gdextension_interface::object_method_bind_ptrcall(worked.bound, p_shape.owner, &arg, nullptr);
				return true;
			}
		}
		return run_bound(worked, p_shape.instance, p_operands, p_count, p_result, r_value, r_error, p_shape.owner);
	}
	return run(p_bind, p_operands, p_count, p_result, r_value, r_error, p_shape.instance);
}

} // namespace kas3
