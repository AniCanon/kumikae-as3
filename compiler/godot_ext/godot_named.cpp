// What one of Godot's values or objects has under an AS3 name (GodotHost::named), for a value a script
// holds untyped: `untyped.name`, `untyped.queueFree()`. The name is looked for in Godot's name table
// (godot/godot_names.txt), under the value's own class and then under each class it extends, as the
// checker finds a member of a typed value: nothing is worked out backward from the AS3 name. What is found
// is kept by class and name, so that a name is looked for once.
#include "godot_calls.h"
#include "program.h"
#include "handles.h"

#if !defined(KAS3_RUNTIME_ONLY)
#include "builtins.h"
#include "godot_names.h"
#include "worked.h"
#endif

#include <godot_cpp/classes/class_db_singleton.hpp>

#include <string>
#include <unordered_map>

using namespace godot;

namespace kas3 {

vm::NamedShape GodotHost::named_shape(void *p_receiver) {
#if defined(KAS3_RUNTIME_ONLY)
	(void)p_receiver;
	return {};
#else
	const Variant &value = *static_cast<const Variant *>(p_receiver);
	const Variant::Type type = value.get_type();
	if (type != Variant::OBJECT) {
		// Calls on other value types may mutate their copy of the receiver.
		return type >= Variant::VECTOR2 && type <= Variant::COLOR ? vm::NamedShape{ static_cast<uintptr_t>(type) + 1 } : vm::NamedShape{};
	}
	void *raw = live_handle_object(p_receiver);
	Object *owner = raw ? internal::get_object_instance_binding(raw) : nullptr;
	if (!owner) {
		return {};
	}
	StringName klass;
	if (!gdextension_interface::object_get_class_name(owner->_owner, gdextension_interface::library, klass._native_ptr())) {
		return {};
	}
	const Variant script = owner->get_script();
	const uint64_t id = script.get_type() == Variant::OBJECT ? gdextension_interface::variant_get_object_instance_id(script._native_ptr()) : 0;
	return { class_identity(klass), id, id ? script_object(owner) : nullptr, owner->_owner };
#endif
}

#if defined(KAS3_RUNTIME_ONLY)

// A build that carries no table of Godot's names can't say: the script is told so.
const vm::NamedMember &GodotHost::named(void *, std::string_view) {
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

const vm::NamedMember &GodotHost::named(void *p_receiver, std::string_view p_name) {
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
	Members &members = object ? class_members_[klass] : value_members_[type];
	if (const auto known = members.find(p_name); known != members.end()) {
		return known->second;
	}
	if (!object) {
		klass = StringName(Variant::get_type_name(type));
	}
	while (klass != StringName()) {
		const std::string godot = String(klass).utf8().get_data();
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
				named_bindings_.push_back(std::move(p_binding));
				return named_bindings_.back().get();
			};
			if (found.what == vm::NamedMember::What::Method) {
				found.invoke = keep_binding(method_of(godot, words, type, false, std::nullopt));
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
				return members.emplace(entry.key.substr(entry.key.find('.') + 1), found).first->second;
			}
		}
		klass = object ? ClassDBSingleton::get_singleton()->get_parent_class(klass) : StringName();
	}
	return members.emplace(*missing_names_.emplace(p_name).first, vm::MISSING_MEMBER).first->second;
}

#endif

} // namespace kas3
