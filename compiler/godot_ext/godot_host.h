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

namespace kas3 {

// A script object as Godot holds it: the Godot object it is attached to, for a class that extends one of
// Godot's (a null one when it has none). False for any other script object, which Godot can't hold.
bool as_godot_object(const vm::Object *p_object, godot::Variant *r_out);
// A script object as Godot holds it: its Godot object, or a vector or an Array as an Array. False, with
// why, for an object of a class that extends none of Godot's.
bool object_element(const vm::Object *p_object, godot::Variant *r_out, std::string *r_error);

// Lets go of the defaults Godot worked out (see GodotHost::evaluate()). Before the extension goes.
void forget_defaults();

class GodotHost final : public vm::Output, public vm::EngineHandles, public vm::EngineSource, public vm::Clock {
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
	// Only a successful ClassDB lookup is cached. Script fields depend on the instance.
	struct {
		std::string_view as3; // Points into the generated static table.
		godot::StringName godot;
		uint64_t owner = 0;
	} last_signal_;
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
	const Name &find_name(std::string_view p_text);
	const godot::StringName &name(std::string_view p_text);
	const godot::Variant *evaluated(std::string_view p_expression, std::string *r_error);
	// name() as a String, made once.
	const godot::String &text_of(std::string_view p_text);

	// One of Godot's global functions, found once: see godot_utility.cpp.
	struct Utility;
	std::unordered_map<std::string_view, std::unique_ptr<Utility>> utilities_;
	const Utility *utility(std::string_view p_signature, std::string *r_error);
	bool call_utility(std::string_view p_signature, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_result,
			vm::Slot *r_result, std::string *r_error);
	bool call_utility(const Utility &p_function, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_result, vm::Slot *r_result,
			std::string *r_error);

public:
	// An engine instruction worked out once (worked.h): public for the helpers of godot_prepared.cpp.
	struct Worked;
	// A program is about to be made (its interpreter asks for every engine instruction it has, once).
	void new_program() { program_++; }
	// The programs made before the last are gone: what was worked out for a call at its own place and
	// is not the last program's is let go of.
	void release_unused();
	// How many calls worked out at their own place are kept, and how many of them are the last program's.
	std::pair<size_t, size_t> own_places() const;

private:
	// Each kind of engine instruction worked out once (godot_prepared.cpp), by what it was worked out from.
	std::unordered_map<std::string, std::unique_ptr<Worked>> worked_;
	const Worked *keep(std::string p_key, std::unique_ptr<Worked> p_worked);
	uint64_t program_ = 0; // Which program is being worked out for: counted up as each is made.
	std::unique_ptr<Worked> work_out(const ir::Module &p_module, const ir::Function &p_function, const ir::Instruction &p_in, std::string *r_key);
	bool run_operator(const Worked &p_worked, const vm::Argument *p_operands, size_t p_count, const vm::Wanted &p_result, vm::Slot *r_result);
	bool run_builtin(const Worked &p_worked, const vm::Argument *p_operands, size_t p_count, const vm::Wanted &p_result, vm::Slot *r_result,
			std::string *r_error);
	bool run_bound(const Worked &p_worked, const vm::Argument *p_operands, size_t p_count, const vm::Wanted &p_result, vm::Slot *r_result,
			std::string *r_error);
	bool run_field(const Worked &p_worked, const vm::Argument *p_operands, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error);
	bool run_construct(const Worked &p_worked, const vm::Argument *p_operands, size_t p_count, const vm::Wanted &p_result, vm::Slot *r_result,
			std::string *r_error);

public:
	GodotHost();
	~GodotHost() override;
	// A handle on an object that does not keep it alive, for a script's hold on what it is attached to: a
	// script on a reference-counted object must not keep that object from being freed. Dropped like any
	// other handle. Copies of it are ordinary handles.
	void *borrow(godot::Object *p_object);
	// Whether a handle on an object is one every holder shares (godot_handles.cpp).
	bool shares(void *p_object_handle);

	// The engine instructions, worked out once and run (godot_prepared.cpp).
	[[nodiscard]] const vm::Prepared *prepare(const ir::Module &p_module, const ir::Function &p_function, const ir::Instruction &p_in) override;
	[[nodiscard]] bool run(const vm::Prepared &p_prepared, const vm::Argument *p_operands, size_t p_count, const vm::Wanted &p_result, vm::Slot *r_result,
			std::string *r_error) override;

	void print(std::string_view p_text) override;
	void error(std::string_view p_text) override;
	void error_at(std::string_view p_text, std::string_view p_function, std::string_view p_file, int32_t p_line) override;
	[[nodiscard]] void *construct(std::string_view p_type, const vm::Argument *p_arguments, size_t p_count, std::string *r_error) override;
	[[nodiscard]] void *attach_new(uint32_t p_class, vm::Object *p_object, std::string *r_error) override;
	[[nodiscard]] void *collection(std::string_view p_type, const ir::Class &p_class, const vm::Vector *p_vector, std::string *r_error) override;
	[[nodiscard]] void *boxed(ir::Bare p_type, vm::Slot p_value, std::string *r_error) override;
	[[nodiscard]] bool unboxed(ir::Bare p_type, void *p_handle, vm::Slot *r_value) override;
	[[nodiscard]] bool call(void *p_receiver, std::string_view p_method, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_result,
			vm::Slot *r_result, std::string *r_error) override;
	[[nodiscard]] bool get(void *p_receiver, std::string_view p_property, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) override;
	[[nodiscard]] bool operate(int p_number, std::string_view p_operator, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_result, vm::Slot *r_result,
			std::string *r_error) override;
	[[nodiscard]] bool set(void *p_receiver, std::string_view p_property, const vm::Argument &p_value, std::string *r_error) override;
	[[nodiscard]] bool index_get(void *p_receiver, const vm::Argument &p_key, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) override;
	[[nodiscard]] bool index_set(void *p_receiver, const vm::Argument &p_key, const vm::Argument &p_value, std::string *r_error) override;
	[[nodiscard]] void *bind(vm::Object *p_object, uint32_t p_function, void *p_owner, std::string *r_error) override;
	[[nodiscard]] bool member(void *p_receiver, std::string_view p_name, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) override;
	[[nodiscard]] bool evaluate(std::string_view p_expression, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) override;
	[[nodiscard]] void *singleton(std::string_view p_class, std::string *r_error) override;
	// Godot's clock; the zone's offset at a moment is the C library's, which knows the past and the future
	// of daylight saving where Godot gives only the offset now.
	double now() override;
	double local_offset(double p_utc) override;
	[[nodiscard]] bool constant(std::string_view p_class, std::string_view p_name, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) override;
	[[nodiscard]] bool call_static(std::string_view p_class, std::string_view p_method, const vm::Argument *p_arguments, size_t p_count,
			const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) override;
	[[nodiscard]] bool present(void *p_value) override;
	[[nodiscard]] bool is_class(void *p_value, std::string_view p_class) override;
	[[nodiscard]] vm::Str *text(void *p_value) override;
	[[nodiscard]] bool keep(vm::Object *p_object) override; // counted.cpp
	[[nodiscard]] vm::Object *script_of(void *p_handle, uint32_t p_class, bool p_taken) override; // counted.cpp
	[[nodiscard]] void *copy(void *p_value) override;
	void drop(void *p_value) override;
	[[nodiscard]] bool assign(void *p_to, const void *p_from) override;
	// For the cycle collector: a reference-counted object that a script listens to or that has a script
	// is a node (by its id), and so is a listener (callable.h).
	[[nodiscard]] uint64_t node(void *p_value) override;
	[[nodiscard]] bool node_edges(uint64_t p_node, vm::EngineNode *r_node) override;
	[[nodiscard]] std::vector<uint64_t> let_go(const std::vector<uint64_t> &p_nodes) override;
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
// One of Godot's value types by its name ("Vector2"); nothing for a name that is not one.
std::optional<int> value_type(std::string_view p_name);

} // namespace kas3
