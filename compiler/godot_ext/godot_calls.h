// What the calls into Godot share, in godot_host.cpp, godot_handles.cpp, godot_signals.cpp and
// godot_source.cpp. Not for other files.
#pragma once

#include "godot_host.h"

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace kas3 {

// The arguments of one call into Godot: as godot::Variants, and as the pointers Godot takes. A call of up to
// eight sits on the stack, so that nothing is allocated for it.
class Arguments {
	static constexpr size_t INLINE = 8;
	godot::Variant inline_values_[INLINE];
	const godot::Variant *inline_pointers_[INLINE];
	std::vector<godot::Variant> more_values_;
	std::vector<const godot::Variant *> more_pointers_;
	const godot::Variant **pointers_ = inline_pointers_;
	size_t count_ = 0;

public:
	Arguments(const vm::Argument *p_arguments, size_t p_count) :
			count_(p_count) {
		godot::Variant *values = inline_values_;
		if (count_ > INLINE) {
			more_values_.resize(count_);
			more_pointers_.resize(count_);
			values = more_values_.data();
			pointers_ = more_pointers_.data();
		}
		for (size_t i = 0; i < p_count; i++) {
			values[i] = to_variant(p_arguments[i].kind, p_arguments[i].slot);
		}
		for (size_t i = 0; i < count_; i++) {
			pointers_[i] = &values[i];
		}
	}
	Arguments(const Arguments &) = delete;
	Arguments &operator=(const Arguments &) = delete;

	const godot::Variant **pointers() { return pointers_; }
	size_t count() const { return count_; }
};

// What reaching through an object Godot has freed is: AS3's null error, which says so.
inline constexpr const char *FREED = "TypeError: Error #1009: Cannot access a property or method of a null object reference. Godot has freed the object.";

// AS3's text for null, which a freed object reads as too.
inline constexpr std::string_view NULL_TEXT = "null";

// An object Godot has freed. To a script it is null.
inline bool is_freed(const godot::Variant &p_value) {
	return p_value.get_type() == godot::Variant::OBJECT && !((godot::ObjectID)p_value).is_null() && !p_value.get_validated_object();
}

// A failed call in AS3's words.
std::string describe(const GDExtensionCallError &p_error, std::string_view p_what);
// Text as Godot holds it.
godot::String to_godot(std::string_view p_text);
// The words of a name-table value, split at its spaces: "position property Vector2 get_position 3341600327 ...".
inline std::vector<std::string_view> godot_words(std::string_view p_value) {
	std::vector<std::string_view> words;
	for (size_t at = 0; at <= p_value.size();) {
		const size_t space = std::min(p_value.find(' ', at), p_value.size());
		words.push_back(p_value.substr(at, space - at));
		at = space + 1;
	}
	return words;
}

// The AS3 way of listening to a signal (`@add`, `@remove` and the rest), for GodotHost::call and get.
bool signal_verb(const godot::Variant &p_receiver, std::string_view p_verb, const vm::Argument *p_arguments, size_t p_count,
		const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error);

} // namespace kas3
