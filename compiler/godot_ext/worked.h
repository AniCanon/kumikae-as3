// An engine instruction as GodotHost worked it out once, when the program was loaded (godot_prepared.cpp):
// what Godot gave for it, found by the facts its entry in the name table carries. Read by godot_prepared.cpp
// alone; here so that GodotHost can let go of them.
#pragma once

#include "godot_host.h"

#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace kas3 {

// A type in a signature that is not one of Godot's value types: any value (Variant), and nothing (void).
inline constexpr int ANY_VALUE = -1;
inline constexpr int NO_VALUE = -2;

// The text last given for a parameter that takes a StringName, and the StringName made of it: an action's
// name written in the program is made into Godot's name once, not at each call.
struct NameKept {
	std::string text;
	godot::Variant name;
};

// The most operands a worked-out call takes on the stack; one with more goes by name.
inline constexpr size_t MOST_OPERANDS = 16;

// Where a Variant of a type keeps its bare value, as Godot's entries read and write it.
void *inside(godot::Variant &p_value, int p_type);

// One of Godot's classes' methods by its hash, if this Godot has it: one that is missing (a class of the
// editor's in an exported game) is left to be reported by name where it is used, not at load.
GDExtensionMethodBindPtr method_bind(std::string_view p_class, std::string_view p_method, std::string_view p_hash);

// The Variants one call holds, made only as they are needed: a Variant is made and let go through Godot,
// so sixteen of them for a call of one argument would cost more than the call.
class Values {
	alignas(godot::Variant) unsigned char memory_[MOST_OPERANDS][sizeof(godot::Variant)];
	size_t made_ = 0;

public:
	Values() = default;
	Values(const Values &) = delete;
	Values &operator=(const Values &) = delete;
	~Values() {
		for (size_t i = 0; i < made_; i++) {
			reinterpret_cast<godot::Variant *>(memory_[i])->~Variant();
		}
	}
	godot::Variant &next() { return *new (memory_[made_++]) godot::Variant(); }
	// The memory of the next, for a value made in it: the caller makes one there before anything else.
	void *room() { return memory_[made_++]; }
};

// The signal last emitted through one emit() (Worked, What::Emit): its name as a StringName and as the
// Variant emit_signal takes, plus its object's id. Compared by fields rather than raw bytes so padding
// bytes in godot::Signal are never read.
struct SignalSeen {
	godot::StringName signal_name;
	godot::Variant name;
	uint64_t object = 0;
	bool any = false;
};

struct GodotHost::Worked final : vm::Prepared {
	enum class What { Operator, Builtin, Bound, Getter, Setter, FieldGet, FieldSet, Constant, Utility, Construct, Emit } what = What::Constant;
	std::string name; // Godot's name for it, for the errors it reports.
	// Operator: Godot's evaluator for these types; the types of its operands, and of what it gives.
	GDExtensionPtrOperatorEvaluator evaluator = nullptr;
	int operands[2] = { NO_VALUE, NO_VALUE };
	// The type of what it gives (a Builtin, a FieldGet, an Operator), and that type's zero to make it in.
	int result = NO_VALUE;
	godot::Variant zero;
	// Builtin: a method of one of Godot's value types, called on the bare value (no receiver: a static one).
	GDExtensionPtrBuiltInMethod builtin = nullptr;
	int receiver = NO_VALUE;
	// Bound, Getter, Setter: a method of a class, found by its hash; the receiver comes first unless it is
	// static. An accessor that serves several properties takes `index` first. Emit: Object's emit_signal.
	GDExtensionMethodBindPtr bound = nullptr;
	mutable SignalSeen seen; // Emit.
	bool is_static = false;
	bool by_name = false; // Object.get/set/call let a script intercept the request.
	// Every parameter and the result are numbers, Booleans or value types held inline (Vector2, Color): with
	// every argument given, the method bind is called on the bare values, with no Variant at all.
	bool bare = false;
	int64_t index = -1;
	// The type of each parameter: a Builtin's are exact; a Bound's say where a StringName is taken.
	std::vector<int> parameters;
	mutable std::vector<NameKept> names; // By parameter, for those that take a StringName.
	// Worked out for one call alone (it keeps that call's names), and the last program that asked for it:
	// an instruction's place is a program's own, so it goes when no program left uses it.
	bool own_place = false;
	mutable uint64_t program = 0;
	// FieldGet, FieldSet: a part of one of Godot's values (Vector2's x).
	GDExtensionPtrGetter getter = nullptr;
	GDExtensionPtrSetter setter = nullptr;
	// Constant: its value; Utility: a global function.
	godot::Variant value;
	const Utility *utility = nullptr;
};

// Shared by typed instructions and members discovered on an untyped receiver. An absent argument
// count defers the built-in method's arity check until it runs.
std::unique_ptr<GodotHost::Worked> method_of(std::string_view p_owner, const std::vector<std::string_view> &p_words, int p_receiver,
		bool p_static, std::optional<size_t> p_arguments);
std::unique_ptr<GodotHost::Worked> member_of(std::string_view p_owner, const std::vector<std::string_view> &p_words, bool p_set, ir::Kind p_receiver);

// A signal's emit(), which Godot has as a method of the signal that takes any number of values: worked out
// as Object's emit_signal, called by its method bind (godot_emit.cpp). Nothing for any other entry.
std::unique_ptr<GodotHost::Worked> emit_of(const ir::EngineName &p_entry, const std::vector<std::string_view> &p_words);
// Emits the signal a handle holds, with values that are Godot's already: `p_pointers` has room for the
// signal's name first, then the `p_count` values. False, with nothing done, where the handle holds no
// signal or the signal's object is gone: what that is, the request by name says.
bool emit_signal(const GodotHost::Worked &p_worked, const godot::Variant *p_signal, const godot::Variant **p_pointers, size_t p_count);

} // namespace kas3
