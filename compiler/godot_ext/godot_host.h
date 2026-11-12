// The engine, as the interpreter sees it: every request the interpreter makes through vm::Host is carried
// out here with Godot's own calls. An engine value is a godot::Variant on the heap.
#pragma once

#include "host.h"

#include <godot_cpp/variant/variant.hpp>

namespace kas3 {

class GodotHost final : public vm::Host {
public:
	void print(std::string_view p_text) override;
	void error(std::string_view p_text) override;
	void *construct(std::string_view p_type, const Argument *p_arguments, size_t p_count, std::string *r_error) override;
	bool call(void *p_receiver, std::string_view p_method, const Argument *p_arguments, size_t p_count, ir::Kind p_result,
			vm::Slot *r_result, std::string *r_error) override;
	bool get(void *p_receiver, std::string_view p_property, ir::Kind p_result, vm::Slot *r_result, std::string *r_error) override;
	bool set(void *p_receiver, std::string_view p_property, const Argument &p_value, std::string *r_error) override;
	void *copy(void *p_value) override;
	void drop(void *p_value) override;
};

// An interpreter value as a Variant, and back. A string or an engine value coming back is the caller's
// to release or drop.
godot::Variant to_variant(ir::Kind p_kind, const vm::Slot &p_slot);
vm::Slot from_variant(ir::Kind p_kind, const godot::Variant &p_value);

} // namespace kas3
