// An engine instruction as GodotHost worked it out once, when the program was loaded (godot_prepared.cpp):
// what Godot gave for it, found by the facts its entry in the name table carries. Read by godot_prepared.cpp
// alone; here so that GodotHost can let go of them.
#pragma once

#include "godot_host.h"

#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <string>
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

struct GodotHost::Worked final : vm::Prepared {
	enum class What { Operator, Builtin, Bound, Getter, Setter, FieldGet, FieldSet, Constant, Utility, Construct } what = What::Constant;
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
	// static. An accessor that serves several properties takes `index` first.
	GDExtensionMethodBindPtr bound = nullptr;
	bool is_static = false;
	// Every parameter and the result are numbers, Booleans or value types held inline (Vector2, Color): with
	// every argument given, the method bind is called on the bare values, with no Variant at all.
	bool bare = false;
	int64_t index = -1;
	// The type of each parameter: a Builtin's are exact; a Bound's say where a StringName is taken.
	std::vector<int> parameters;
	mutable std::vector<NameKept> names; // By parameter, for those that take a StringName.
	// FieldGet, FieldSet: a part of one of Godot's values (Vector2's x).
	GDExtensionPtrGetter getter = nullptr;
	GDExtensionPtrSetter setter = nullptr;
	// Constant: its value; Utility: a global function.
	godot::Variant value;
	const Utility *utility = nullptr;
};

} // namespace kas3
