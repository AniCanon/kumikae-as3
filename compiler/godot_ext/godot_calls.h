// What the calls into Godot share, in godot_host.cpp and godot_source.cpp. Not for other files.
#pragma once

#include "godot_host.h"

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

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

// A failed call in AS3's words.
std::string describe(const GDExtensionCallError &p_error, std::string_view p_what);
// Text as Godot holds it.
godot::String to_godot(std::string_view p_text);

} // namespace kas3
