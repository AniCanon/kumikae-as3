// What each object alive was constructed with: the arguments its constructor was called with, and what
// its fields were when the constructor returned (its first values). For a reload that updates objects in
// place (vm/reload.h, update_in_place): the new constructor is run again with the same arguments on a
// throwaway object, and a field the program has not changed since takes its new first value.
//
// Kept with the object, in room after its own slots that it is made with only while a host asks for this
// (Interpreter::remember_constructions, Heap::object_slots): the object's header and its own slots are as
// they are without it, nothing is looked up or allocated for an object made or freed, and native code keeps
// none. How much room an object of a class has, and which of its values can be other than a number, is
// worked out once for each class.
//
// Of an object whose class's constructor chain can be run again (ir::constructor_refusal: it depends on
// nothing but its arguments and constants), the arguments alone are kept: its first values are what the
// chain gives for them, and a reload that needs them runs the chain of the program that made the object,
// which is still there. (The one case where that is not what the constructor left: an object made while a
// class's static fields were being given their values, by a chain that reads a constant not yet given
// its own. Its field then looks changed by the program, and is left alone.) Of any other object the first
// values are kept as the constructor returns, so that an edit after which the chain can be run again
// still updates it.
//
// Only plain values are kept (numbers, Booleans, text, null), so nothing kept holds an object or anything
// of Godot's: an object made with anything else, or whose first values hold anything else, has no record.
// Text is held as the String itself, which never changes and holds nothing.
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
	// field of its class, each as its parameter or field holds it. The constructor is its class's.
	struct Record {
		uint32_t kept; // What of it there is: one of the four below.
		uint32_t arguments; // How many were passed, the object itself not counted.
		Slot values[1]; // As many as the class has room for: its constructor's parameters and its fields.
	};

	// NOTHING: as the object is made. ARGUMENTS: its arguments; the first values are what the program's
	// chain gives for them (where it can be run again), or the constructor has not returned yet. ALL: its
	// first values too. CARRIED: the arguments of an object a reload moved here, whose first values are
	// what the chain of the program it came from gives; the reload settles each before it ends (reload.h).
	static constexpr uint32_t NOTHING = 0, ARGUMENTS = 1, ALL = 2, CARRIED = 3;

	explicit Constructions(const ir::Module &p_module);
	Constructions(const Constructions &) = delete;
	Constructions &operator=(const Constructions &) = delete;

	// How many slots more than its own an object of the class is made with, for its record: none for a
	// class whose objects are not remembered (the library's, one with a Godot base, one whose constructor
	// takes any number of arguments).
	size_t room(uint32_t p_class) const { return plans_[p_class].room; }

	// A call is being made: if it is the constructor of the class of the object it is called on (not a
	// `super(...)` call), and that is a class whose objects are remembered, its arguments are kept.
	// True if they were: constructed() is then owed when the call returns.
	bool begun(uint32_t p_function, const Slot *p_arguments, size_t p_count) {
		return owner_[p_function] != ir::NONE && keep_arguments(owner_[p_function], p_arguments, p_count);
	}
	// That call returned: the object's fields are kept as its first values.
	void constructed(Object *p_object);
	// The object is being freed, or is not to be remembered after all.
	void forget(Object *p_object) {
		const Plan &plan = plans_[p_object->klass];
		if (plan.room == 0) {
			return;
		}
		Record *record = record_at(p_object, plan);
		if (record->kept != NOTHING) {
			if (!plan.plain_arguments || record->kept == ALL) {
				give_up(plan, record);
			}
			record->kept = NOTHING;
			used_--;
		}
	}
	// The object's record, if what it was constructed with is known from it: all of it, or the arguments
	// of an object whose first values the program's chain gives. Not one a reload has yet to settle.
	const Record *of(const Object *p_object);
	// The object's record whatever there is of it, for a reload; null where an object of its class has
	// no room for one.
	Record *record(Object *p_object) {
		const Plan &plan = plans_[p_object->klass];
		return plan.room != 0 ? record_at(p_object, plan) : nullptr;
	}
	// The object's first values are no longer kept: its arguments are, and its class's chain gives the rest.
	void keep_arguments_only(Object *p_object);
	// A record for an object of the class, made by that constructor with that many arguments, in the place
	// of any it had: the caller gives it each argument, and then either each of the class's fields' first
	// values, saying it has them ALL, or says it is CARRIED. For a reload. Null where an object of the
	// class has no room for one.
	Record *add(Object *p_object, uint32_t p_function, uint32_t p_class, uint32_t p_arguments);
	// What the n-th value of a record of an object of the class is: an argument as its parameter holds
	// it, or a field's first value.
	ir::Kind kind_of(uint32_t p_class, const Record &p_record, uint32_t p_value) const;
	size_t size() const { return used_; } // How many objects have one.

	// Whether a value of the kind is one that is kept: a number, a Boolean, text, or nothing at all.
	static bool plain(ir::Kind p_kind, Slot p_value);
	// Whether two plain values of one kind are the same: numbers by their bits, so that NaN is itself;
	// text by what it says.
	static bool same(ir::Kind p_kind, Slot p_a, Slot p_b);
	// The value held once more, for a record or for the program: only text is counted.
	static Slot held(ir::Kind p_kind, Slot p_value);

private:
	// A value that may be other than a number or a Boolean: the one kind that is looked at before it is
	// kept, and counted if it is text.
	struct Looked {
		uint32_t value; // Which argument, or which field.
		ir::Kind kind;
	};
	// Whether a class's constructor chain can be run again: asked of the module when an object of the
	// class is first made, not of every class as a program is loaded.
	enum class Chain : uint8_t { Unknown, Again, Once };
	// A class whose objects are remembered: where an object's record is and what it has room for.
	struct Plan {
		uint32_t room = 0; // In slots: the record's first two numbers, the most arguments, the fields. 0: not remembered.
		uint32_t at = 0; // The slot the record starts at: the first after the object's own.
		uint32_t constructor = ir::NONE;
		uint32_t most = 0; // Arguments its constructor takes.
		Chain chain = Chain::Unknown;
		bool plain_arguments = false; // None of its arguments is looked at: each is a number or a Boolean.
		// Its chain can be run again and none of its arguments is looked at: an object's arguments are
		// copied, and that is all.
		bool quick = false;
		// In looked_: the arguments to look at, in order, then the fields.
		uint32_t looked_arguments = 0;
		uint32_t looked_fields = 0;
		uint32_t looked_end = 0;
	};
	const ir::Module &module_;
	std::vector<uint32_t> owner_; // By function: the class it is the constructor of, if its objects are remembered.
	std::vector<Plan> plans_; // By class.
	std::vector<Looked> looked_;
	size_t used_ = 0;

	static Record *record_at(Object *p_object, const Plan &p_plan) {
		return reinterpret_cast<Record *>(&slot_at(p_object, p_plan.at));
	}
	Chain chain_of(Plan &p_plan, uint32_t p_class);
	bool keep_arguments(uint32_t p_class, const Slot *p_arguments, size_t p_count);
	// The same, for a class whose arguments are looked at, whose chain is not known yet or can't be run
	// again, or an object that has a record already.
	bool keep_looked(Plan &p_plan, uint32_t p_class, Object *p_object, const Slot *p_arguments, size_t p_count);
	void give_up(const Plan &p_plan, Record *p_record); // Its text given up: what it holds is no longer its own.
};

} // namespace vm
} // namespace kas3
