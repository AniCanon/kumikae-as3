// The engine, as the interpreter sees it: every request the interpreter makes through vm::Host is carried
// out here with Godot's own calls. An engine value is a godot::Variant on the heap.
#pragma once

#include "host.h"

#include <godot_cpp/variant/variant.hpp>

#include <godot_cpp/variant/string_name.hpp>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace kas3 {

// Lets go of the defaults Godot worked out (see GodotHost::evaluate()). Before the extension goes.
void forget_defaults();

class GodotHost final : public vm::Output, public vm::EngineHandles, public vm::EngineSource {
	std::unordered_set<void *> borrowed_; // Handles made by borrow().

	// Godot's form of a name the interpreter passes on every call ("move_and_slide"), made once. The
	// interpreter's names are its module's own text, which stays put while the program is loaded, so the
	// place the text sits is the key; the text is kept beside it and checked, so that a different name at
	// the same place is never mistaken for it.
	struct Name {
		std::string text;
		godot::StringName godot;
		godot::String godot_text; // The same name as a String, for calls that take one.
		bool made = false;
	};
	struct Place {
		const char *data;
		size_t size;
		bool operator==(const Place &p_other) const { return data == p_other.data && size == p_other.size; }
	};
	struct PlaceHash {
		size_t operator()(const Place &p_place) const { return std::hash<const char *>()(p_place.data) ^ (p_place.size * 0x9E3779B97F4A7C15ull); }
	};
	std::unordered_map<Place, Name, PlaceHash> names_;
	// What evaluate() worked out, by the place of its text: the values are kept for the whole run (see
	// forget_defaults()), so a program loaded again asks Godot nothing.
	struct Evaluated {
		std::string text;
		const godot::Variant *value = nullptr;
	};
	std::unordered_map<Place, Evaluated, PlaceHash> evaluated_;
	// A class's static method, by "Class.method hash", found once: called through it, a wrong call is an
	// error the script is told of.
	std::unordered_map<std::string, GDExtensionMethodBindPtr> static_methods_;
	const godot::StringName &name(std::string_view p_text);
	const godot::Variant *evaluated(std::string_view p_expression, std::string *r_error);
	const godot::String &text_of(std::string_view p_text); // name() as a String, made once.

	// One of Godot's global functions, found once: see godot_utility.cpp.
	struct Utility;
	std::unordered_map<std::string_view, std::unique_ptr<Utility>> utilities_;
	const Utility *utility(std::string_view p_signature, std::string *r_error);
	bool call_utility(std::string_view p_signature, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_result,
			vm::Slot *r_result, std::string *r_error);

public:
	GodotHost();
	~GodotHost() override;
	// A handle on an object that does not keep it alive, for a script's hold on what it is attached to: a
	// script on a reference-counted object must not keep that object from being freed. Dropped like any
	// other handle. Copies of it are ordinary handles.
	void *borrow(godot::Object *p_object);

	void print(std::string_view p_text) override;
	void error(std::string_view p_text) override;
	void error_at(std::string_view p_text, std::string_view p_function, std::string_view p_file, int32_t p_line) override;
	void *construct(std::string_view p_type, const vm::Argument *p_arguments, size_t p_count, std::string *r_error) override;
	void *collection(std::string_view p_type, ir::Kind p_kind, const vm::Slot *p_elements, size_t p_count, std::string *r_error) override;
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
	bool evaluate(std::string_view p_expression, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) override;
	void *singleton(std::string_view p_class, std::string *r_error) override;
	bool constant(std::string_view p_class, std::string_view p_name, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) override;
	bool call_static(std::string_view p_class, std::string_view p_method, const vm::Argument *p_arguments, size_t p_count,
			const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) override;
	bool present(void *p_value) override;
	bool is_class(void *p_value, std::string_view p_class) override;
	vm::Str *text(void *p_value) override;
	void *copy(void *p_value) override;
	void drop(void *p_value) override;
};

// An interpreter value as a Variant, and back. A string or an engine value coming back is the caller's
// to release or drop. Nothing comes back when there was no memory to hold it.
godot::Variant to_variant(ir::Kind p_kind, const vm::Slot &p_slot);
std::optional<vm::Slot> from_variant(ir::Kind p_kind, const godot::Variant &p_value);
godot::Variant untyped_to_variant(const vm::Slot &p_slot);
std::optional<vm::Slot> untyped_from_variant(const godot::Variant &p_value);
// Whether a value arriving from Godot may be taken as this kind, and for an engine value as the type of
// this name ("Vector2", "Node"; empty for any). Numbers are taken as GDScript itself would take them, but
// one that does not fit an int or a uint is refused, never wrapped.
bool arrives(ir::Kind p_kind, std::string_view p_godot_type, const godot::Variant &p_value);
// What Godot gave, as the interpreter asked for it; refused, with a message that names p_from, if it is
// something else.
bool take(const vm::Wanted &p_wanted, const godot::Variant &p_value, std::string_view p_from, vm::Slot *r_result, std::string *r_error);
// One of Godot's value types by its name ("Vector2"), or -1.
int value_type(std::string_view p_name);

} // namespace kas3
