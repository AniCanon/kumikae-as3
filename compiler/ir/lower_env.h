// The lowering's places (where a value is stored), and what it keeps of the variables function expressions
// capture (lower_closures.cpp).
#pragma once

#include "ir.h"
#include "names.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace kas3 {
namespace ir {

struct Part;
// A place a value can be stored: a local, a field of an object, or a global.
struct Place {
	// Element, Length and Fixed are of a Vector: `object` holds it, and for Element `key` the index.
	// Accessor: a property of the program's, read by its getter and written by its setter.
	enum Where { Nowhere, Register, FieldOf, GlobalSlot, Engine, Element, Length, Fixed, Accessor } where = Nowhere;
	uint32_t index = 0; // The register, the field slot, the global, or the string of Godot's name.
	uint32_t object = 0; // For FieldOf and Engine: the register holding the object.
	Kind kind = Kind::F64;
	bool of_value = false; // For Engine: a member of a value type, which is a copy.
	const Type *type = nullptr; // For Engine: what the member is declared to be.
	uint32_t key = NONE; // For Engine: the place is an element (`value[key]`); the register holding the key.
	bool constant = false; // One of Godot's constants: read from a register, and never written.
	// For Engine: the script object whose Godot object `object` is, fetched when the place is read or
	// written, so that a missing one is reported after the value is worked out, as in AS3.
	uint32_t script_object = NONE;
	// For Engine: the member is one part of a Godot value that is itself stored somewhere
	// (`position.x`). Godot's values are copies, so writing the part is: read the whole value, change
	// the part, write the whole value back.
	Part *part = nullptr; // In parts_.
	// For Accessor: the getter and the setter (NONE where there is none) and how each is reached: a function
	// called as it is, a method-table slot of the object's class, or an interface method's number.
	// `object` is NONE for a static property.
	enum Dispatch { Direct, Virtual, Interface } dispatch = Direct;
	uint32_t getter = NONE;
	uint32_t setter = NONE;
};
struct Part {
	Place whole; // Where the value is stored.
	uint32_t value = NONE; // The register it was read into, once it has been.
};

// The function a symbol declares, if it declares one.
inline FunctionDecl *declaration(const Symbol *p_symbol) {
	return p_symbol && p_symbol->node && p_symbol->node->kind == NodeKind::FunctionDecl ? static_cast<FunctionDecl *>(p_symbol->node)
																						: nullptr;
}

// The name a method's function and table slot go by: its own, or "get x" and "set x" for an accessor, as
// Flash names a getter's frame in a stack trace.
inline std::string member_key(const FunctionDecl *p_function) {
	const char *half = p_function->accessor == FunctionDecl::Getter ? "get " : p_function->accessor == FunctionDecl::Setter ? "set " : "";
	return half + std::string(p_function->name);
}

// An environment's class: the variables it holds, each by its field. The first field is the outer one.
struct EnvLayout {
	uint32_t klass = NONE;
	std::unordered_map<const Symbol *, uint32_t> fields;
};

// A function expression met while lowering a function: lowered once that function is done.
struct PendingFunction {
	FunctionDecl *decl = nullptr;
	uint32_t index = NONE; // Its function, already in the module.
	Unit *unit = nullptr;
	Symbol *klass = nullptr; // The class it is written in, if any.
	std::vector<EnvLayout> chain; // The environments it can reach, nearest first.
};

} // namespace ir
} // namespace kas3
