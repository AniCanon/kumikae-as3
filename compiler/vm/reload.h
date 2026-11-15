// Moving a running program onto a newer build of itself: what a game being edited needs.
#pragma once

#include "interpreter.h"

#include <string>
#include <unordered_map>

namespace kas3 {
namespace vm {

// Built between two interpreters that share a host: the one that has been running, and one made for the
// new build that has run nothing yet. Neither may be in the middle of a call.
//
// An object is moved by making one of the class of the same name in the new program and giving it what
// the old one held, field by field, by name. A field that is new, or whose kind changed, starts empty:
// the object is not constructed again, so that what the game did to it is not undone. Objects that refer
// to each other are each moved once.
class Reload {
	Interpreter &from_;
	Interpreter &to_;
	std::unordered_map<std::string_view, uint32_t> classes_; // In the new program, by name.
	std::unordered_map<std::string_view, uint32_t> functions_;
	std::unordered_map<Object *, Object *> moved_; // Each holds one reference to the new object.
	std::string missing_;

	// What a field or a static field held, as the new one holds it: nothing, if the new one is declared
	// to hold another class of object than the one that was there.
	Slot carry_into(ir::Kind p_kind, uint32_t p_class, Slot p_value);
	Slot carry(ir::Kind p_kind, Slot p_value);

public:
	Reload(Interpreter &p_from, Interpreter &p_to);
	~Reload();
	Reload(const Reload &) = delete;
	Reload &operator=(const Reload &) = delete;

	// The object's counterpart in the new program, with a reference of the caller's own. Null for null,
	// and for an object whose class the new program does not have: missing() then names the class.
	Object *object(Object *p_old);
	// Static fields and package variables, by name. A class whose static fields had been given their
	// values is not given them again; one of its fields that is new starts empty.
	void globals();
	// Where a function of the old program is in the new one, by its name. NONE if it is gone.
	uint32_t function(uint32_t p_old) const;
	const std::string &missing() const { return missing_; }
	// Whether an object of one of these classes (named as the new program names them) has been moved so
	// far: one that is alive.
	bool alive(const std::vector<std::string> &p_classes) const;
};

} // namespace vm
} // namespace kas3
