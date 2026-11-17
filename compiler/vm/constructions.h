// What each object alive was constructed with: the arguments its constructor was called with and its
// fields as they were when the constructor returned (its first values). For a reload that updates objects
// in place (vm/reload.h, update_in_place): the new constructor is run again with the same arguments on a
// throwaway object, and a field the program has not changed since takes its new first value.
//
// A table beside the objects, the interpreter's own, kept only while a host asks for it
// (Interpreter::remember_constructions): the object's header and slots are as they are without it, and
// native code keeps none. Only plain values are kept (numbers, Booleans, text, null), so nothing kept
// holds an object or anything of Godot's: an object made with anything else, or whose first values hold
// anything else, has no record. Text is held as the String itself, which never changes and holds nothing.
#pragma once

#include "ir.h"
#include "runtime.h"

#include <cstdint>
#include <vector>

namespace kas3 {
namespace vm {

class Constructions {
public:
	// One object's: the constructor's arguments after the object itself, then one first value for each
	// field of its class, each as its parameter or field holds it.
	struct Record {
		uint32_t function; // The constructor that was called: its parameters say what the arguments are.
		uint32_t klass; // The object's class: its fields say what the first values are.
		uint32_t arguments; // How many were passed, the object itself not counted.
		uint32_t fields; // How many first values follow them: NOT_YET until the constructor has returned.
		Slot values[1]; // As many as its size class has: Record is allocated with room for them.
	};

	static constexpr uint32_t NOT_YET = 0xFFFFFFFFu;

	explicit Constructions(const ir::Module &p_module);
	~Constructions();
	Constructions(const Constructions &) = delete;
	Constructions &operator=(const Constructions &) = delete;

	// A call is being made: if it is the constructor of the class of the object it is called on (not a
	// `super(...)` call), and that is a class of the program's with no Godot base, its arguments are kept.
	// True if they were: constructed() is then owed when the call returns.
	bool begun(uint32_t p_function, const Slot *p_arguments, size_t p_count);
	// That call returned: the object's fields are kept as its first values.
	void constructed(Object *p_object);
	// The object is being freed, or is not to be remembered after all.
	void forget(const Object *p_object);
	// The object's record, if it has one whose constructor returned.
	const Record *of(const Object *p_object) const;
	// A record for an object of the class, made by that constructor with that many arguments: the object's
	// own from here on, in the place of any it had, with room for the arguments and for each of the class's
	// fields. For a reload, which fills it in.
	Record *add(const Object *p_object, uint32_t p_function, uint32_t p_class, uint32_t p_arguments);
	// What the n-th value of a record is: an argument as its parameter holds it, or a field's first value.
	ir::Kind kind_of(const Record &p_record, uint32_t p_value) const;
	// Gives one object's record to another, in the place of any that one had.
	void hand_over(const Object *p_from, const Object *p_to);
	size_t size() const { return used_; } // How many objects have one.

	// Whether a value of the kind is one that is kept: a number, a Boolean, text, or nothing at all.
	static bool plain(ir::Kind p_kind, Slot p_value);
	// Whether two plain values of one kind are the same: numbers by their bits, so that NaN is itself;
	// text by what it says.
	static bool same(ir::Kind p_kind, Slot p_a, Slot p_b);
	// The value held once more, for a record or for the program: only text is counted.
	static Slot held(ir::Kind p_kind, Slot p_value);

private:
	struct Entry {
		const Object *object = nullptr;
		Record *record = nullptr;
	};
	const ir::Module &module_;
	std::vector<uint8_t> constructors_; // By function: the constructor of a class whose objects are remembered.
	// Open addressing, by the object's address: a construction costs no allocation once the table and
	// the spare records have grown.
	std::vector<Entry> table_;
	size_t used_ = 0;
	std::vector<std::vector<Record *>> spare_; // Records let go of, by how many values they have room for.

	size_t home(const Object *p_object) const; // Where a search for the object starts.
	size_t place(const Object *p_object) const; // Where the object is, or the empty entry it would go.
	void grow();
	uint32_t room(const Record &p_record) const; // How many values it was allocated for.
	void let_go(Record *p_record); // Its text given up; the record spare.
	void remove(size_t p_place);
};

} // namespace vm
} // namespace kas3
