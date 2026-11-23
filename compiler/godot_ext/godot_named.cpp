// What one of Godot's values or objects has under an AS3 name (GodotHost::named), for a value a script
// holds untyped: `untyped.name`, `untyped.queueFree()`. The name is looked for in Godot's name table
// (godot/godot_names.txt), under the value's own class and then under each class it extends, as the
// checker finds a member of a typed value: nothing is worked out backward from the AS3 name. What is found
// is kept by class and name, so that a name is looked for once.
#include "godot_calls.h"

#if !defined(KAS3_RUNTIME_ONLY)
#include "builtins.h"
#include "godot_names.h"
#endif

#include <godot_cpp/classes/class_db_singleton.hpp>

#include <string>
#include <unordered_map>

using namespace godot;

namespace kas3 {

#if defined(KAS3_RUNTIME_ONLY)

// A build that carries no table of Godot's names can't say: the script is told so.
vm::NamedMember GodotHost::named(void *, std::string_view) {
	vm::NamedMember unknown;
	unknown.what = vm::NamedMember::What::Unknown;
	return unknown;
}

#else

namespace {

struct Found {
	vm::NamedMember::What what = vm::NamedMember::What::Missing;
	std::string godot;
	bool read = true;
	bool written = true;
};

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
Found described(std::string_view p_value) {
	const std::vector<std::string_view> words = godot_words(p_value);
	Found found;
	found.godot = std::string(words[0]);
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

vm::NamedMember GodotHost::named(void *p_receiver, std::string_view p_name) {
	static std::unordered_map<std::string, Found> kept;
	const Variant &receiver = *static_cast<const Variant *>(p_receiver);
	Object *object = receiver.get_type() == Variant::OBJECT ? receiver.get_validated_object() : nullptr;
	StringName klass = object ? StringName(object->get_class()) : StringName(Variant::get_type_name(receiver.get_type()));
	const std::string own = std::string(String(klass).utf8().get_data()) + "." + std::string(p_name);
	auto known = kept.find(own);
	for (; known == kept.end(); klass = object ? ClassDBSingleton::get_singleton()->get_parent_class(klass) : StringName()) {
		if (klass == StringName()) {
			known = kept.emplace(own, Found()).first; // No class of its has the name.
			break;
		}
		const std::string godot = String(klass).utf8().get_data();
		const auto as3 = renamed().find(godot);
		const std::optional<std::string_view> value = godot_value((as3 == renamed().end() ? godot : as3->second) + "." + std::string(p_name));
		if (const Found found = value ? described(*value) : Found(); found.what != vm::NamedMember::What::Missing) {
			known = kept.emplace(own, found).first;
		}
	}
	vm::NamedMember member;
	member.what = known->second.what;
	member.godot = known->second.godot;
	member.read = known->second.read;
	member.written = known->second.written;
	return member;
}

#endif

} // namespace kas3
