#include "interpreter_internal.h"
#include "table.h"

#include <optional>

namespace kas3 {
namespace vm {

using ir::Function;
using ir::Instruction;
using ir::Kind;

// Why Godot can't hold an untyped value, if it can't: a script object of a class that extends none of
// Godot's, an Array with properties beyond its indices, or a container that holds itself. An Array, a
// Vector, an Object and a Dictionary go as Godot's Arrays and Dictionaries (godot_ext/godot_collections.cpp).
std::optional<std::string> Interpreter::godot_refuses(Slot p_value, int p_depth) const {
	const Object *object = held_object(p_value);
	if (!object) {
		return std::nullopt;
	}
	if (p_depth > MAX_ENGINE_NESTING) {
		return std::string(HOLDS_ITSELF);
	}
	const ir::Class &klass = module_.classes[object->klass];
	std::optional<std::string> why;
	const auto each = [&](Slot p_held) {
		if (!why) {
			why = godot_refuses(p_held, p_depth + 1);
		}
	};
	if (klass.table) {
		for (const Entry &entry : entries_of(const_cast<Object *>(object))->all()) {
			each(entry.key);
			each(entry.value);
		}
		return why;
	}
	if (klass.element != Kind::Void) {
		const Vector *vector = static_cast<const Vector *>(object);
		if (vector->properties && vector->properties->size()) {
			return std::string(ARRAY_PROPERTIES);
		}
		for (uint32_t i = 0; i < vector->length && (klass.element == Kind::Any || klass.element == Kind::Obj); i++) {
			each(klass.element == Kind::Any ? vector->data[i] : any::object(static_cast<Object *>(vector->data[i].p)));
		}
		return why;
	}
	if (klass.native_base.empty()) {
		return std::string("a script object can't be given to Godot: only Godot's own values and objects can.");
	}
	return std::nullopt;
}

// An untyped value among a request's operands that Godot can't hold: said before Godot is asked.
std::optional<std::string> Interpreter::refused_by_godot(const Function &p_function, const Instruction &p_in, const Slot *p_registers) const {
	const char *shape = ir::op_shape(p_in.op);
	const uint32_t operands[3] = { p_in.a, p_in.b, p_in.c };
	const auto refused = [&](uint32_t p_register) {
		return p_function.registers[p_register] == Kind::Any ? godot_refuses(p_registers[p_register], 0) : std::nullopt;
	};
	for (int n = 1; n < 3; n++) {
		if (shape[n] == 'R') {
			if (std::optional<std::string> why = refused(operands[n])) {
				return why;
			}
		}
		if (shape[n] == 'A') {
			for (uint32_t each : p_function.argument_lists[operands[n]]) {
				if (std::optional<std::string> why = refused(each)) {
					return why;
				}
			}
		}
	}
	return std::nullopt;
}

} // namespace vm
} // namespace kas3
