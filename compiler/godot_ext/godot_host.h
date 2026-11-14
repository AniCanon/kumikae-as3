// The engine, as the interpreter sees it: every request the interpreter makes through vm::Host is carried
// out here with Godot's own calls. An engine value is a godot::Variant on the heap.
#pragma once

#include "host.h"

#include <godot_cpp/variant/variant.hpp>

#include <unordered_set>

namespace kas3 {

class GodotHost final : public vm::Output, public vm::EngineValues, public vm::EngineObjects {
	std::unordered_set<void *> borrowed_; // Handles made by borrow().

public:
	// A handle on an object that does not keep it alive, for a script's hold on what it is attached to: a
	// script on a reference-counted object must not keep that object from being freed. Dropped like any
	// other handle. Copies of it are ordinary handles.
	void *borrow(godot::Object *p_object);

	void print(std::string_view p_text) override;
	void error(std::string_view p_text) override;
	void *construct(std::string_view p_type, const vm::Argument *p_arguments, size_t p_count, std::string *r_error) override;
	bool call(void *p_receiver, std::string_view p_method, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_result,
			vm::Slot *r_result, std::string *r_error) override;
	bool get(void *p_receiver, std::string_view p_property, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) override;
	bool operate(std::string_view p_operator, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_result, vm::Slot *r_result,
			std::string *r_error) override;
	bool set(void *p_receiver, std::string_view p_property, const vm::Argument &p_value, std::string *r_error) override;
	bool index_get(void *p_receiver, const vm::Argument &p_key, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) override;
	bool index_set(void *p_receiver, const vm::Argument &p_key, const vm::Argument &p_value, std::string *r_error) override;
	void *bind(vm::Object *p_object, uint32_t p_function, void *p_owner, std::string *r_error) override;
	bool member(void *p_receiver, std::string_view p_name, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) override;
	void *singleton(std::string_view p_class, std::string *r_error) override;
	bool constant(std::string_view p_class, std::string_view p_name, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) override;
	bool call_static(std::string_view p_class, std::string_view p_method, const vm::Argument *p_arguments, size_t p_count,
			const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) override;
	vm::Str *text(void *p_value) override;
	void *copy(void *p_value) override;
	void drop(void *p_value) override;
};

// An interpreter value as a Variant, and back. A string or an engine value coming back is the caller's
// to release or drop.
godot::Variant to_variant(ir::Kind p_kind, const vm::Slot &p_slot);
vm::Slot from_variant(ir::Kind p_kind, const godot::Variant &p_value);
// Whether a value arriving from Godot may be taken as this kind, and for an engine value as the type of
// this name ("Vector2", "Node"; empty for any). Numbers are taken as GDScript itself would take them, but
// one that does not fit an int or a uint is refused, never wrapped.
bool arrives(ir::Kind p_kind, std::string_view p_godot_type, const godot::Variant &p_value);

} // namespace kas3
