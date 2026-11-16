// The properties of an Object or a flash.utils.Dictionary (a vm::Table), and an Array's other than its
// indices: keys and values in the order each key was first set. AS3 leaves that order unspecified; keeping
// it is this interpreter's ruling, so that a program's output does not depend on a hash.
#pragma once

#include "any.h"
#include "fields.h"
#include "runtime.h"

#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kas3 {
namespace vm {

// One property. Both slots are untyped values (vm/any.h): the key a String, or for a Dictionary an object
// kept by its identity, and the value anything. A deleted one keeps its place with both undefined until
// the entries are packed again.
struct Entry {
	Slot key;
	Slot value;
	// setPropertyIsEnumerable(name, false): passed over by `for in`, `for each` and JSON; there for the rest.
	bool hidden = false;
};

// Holds a reference to each key and value; letting them go is the interpreter's, which knows how.
class Entries {
	std::vector<Entry> entries_;
	std::unordered_map<std::string_view, uint32_t> texts_; // A String key's entry, by its text (the key's own bytes).
	std::unordered_map<const void *, uint32_t> objects_; // An object key's entry, by the object.
	uint32_t live_ = 0;

	void pack();

public:
	// Every entry there is, deleted ones too (their key undefined). What a collector must visit is here,
	// and only here: each key that is an object, and each value.
	const std::vector<Entry> &all() const { return entries_; }
	std::vector<Entry> &all() { return entries_; } // For visiting: a key's place is never changed through it.
	uint32_t size() const { return live_; }
	std::optional<uint32_t> find_text(std::string_view p_text) const;
	std::optional<uint32_t> find_object(const void *p_object) const;
	Slot &value_at(uint32_t p_at) { return entries_[p_at].value; }
	// A new property, last in order. It takes over the references the key and the value hold. Positions
	// found before may change.
	void add(Slot p_key, Slot p_value, bool p_hidden = false);
	// Takes a property out: the references it held are the caller's.
	Entry remove(uint32_t p_at);
	// Takes every property out, in order: the references are the caller's.
	std::vector<Entry> take_all();
};

// An Object or a Dictionary: an object of a class marked `table`, with no fields of its own; its one slot
// holds its Entries.
Object *new_table(uint32_t p_class, Object *p_list); // One reference, the caller's, on the list (as new_object()). Null if out of memory.
inline Entries *entries_of(Object *p_table) {
	return static_cast<Entries *>(own_field(p_table, 0).p);
}
void free_table(Object *p_table); // Its entries must have been taken out.
// An Array's properties other than its indices, made the first time one is set. Null if out of memory.
Entries *properties_of(Vector *p_array);
void free_properties(Vector *p_array); // Its properties must have been taken out.

} // namespace vm
} // namespace kas3
