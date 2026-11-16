// A method of a script object as a Godot callable, as callable.cpp and listeners.cpp share it. Not for
// other files: callable.h is what the rest of the extension uses.
#pragma once

#include "callable.h"
#include "program.h"

#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable_custom.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/string.hpp>

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kas3 {

class BoundMethod;
// Every bound method there is, so that the ones a program leaves behind can be dealt with.
std::unordered_set<BoundMethod *> &bound_methods();
// The same, by the number the cycle collector knows each by.
std::unordered_map<uint64_t, BoundMethod *> &by_number();

class BoundMethod : public godot::CallableCustom {
	friend void kas3::listener_added(const godot::Callable &, const godot::Signal &);
	friend void kas3::release_listeners();
	friend bool kas3::listeners_can_move(vm::Reload &);
	friend void kas3::move_listeners(vm::Interpreter &, vm::Reload &, uint64_t);
	friend void kas3::listeners_of(godot::Object *, std::vector<uint64_t> *);
	friend bool kas3::listener_edges(uint64_t, vm::EngineNode *);
	friend class ListenerRemoval;
	friend void kas3::forget_listeners(const vm::Object *);
	friend bool kas3::listener_belongs_to(const godot::Callable &, const vm::Object *, uint32_t);

	vm::Object *object_;
	// For a script object whose Godot object is reference-counted (counted_on()): the callable does not hold
	// the script object, as a callable of Godot's does not hold its object. It goes when that Godot object goes.
	bool weak_ = false;
	uint32_t function_;
	godot::ObjectID owner_; // Null for a class with no Godot object behind it.
	uint64_t program_; // Which load of the project's scripts the object belongs to.
	// What makes two of these the same listener, and what Godot files them under. It must not change
	// while a signal holds the callable, so it is not the object and function above: those are replaced
	// when the scripts are reloaded. It is what they were when the method was first bound.
	const vm::Object *key_object_;
	uint32_t key_function_;
	uint64_t key_program_;
	// The cycle collector's number for it (vm::EngineNode). Counted from 1, it never has the top bit set,
	// which every id of a reference-counted object has: the two are never taken for each other.
	uint64_t number_;
	// The signals it is known to have been in: those a script added it to, and those the cycle collector
	// found it in (GDScript may connect a copy of it). It may have been taken out of some since.
	std::vector<godot::Signal> signals_;

	// Lets go of the script object. After this the callable refuses to run.
	void let_go();

	// Whether the script object can still be used: its program is the one loaded, and its owner lives.
	bool alive() const {
		return object_ && Project::generation() == program_ && (owner_.is_null() || godot::ObjectDB::get_instance(owner_) != nullptr);
	}

	// A listener the cycle collector looks through: one with no Godot object of its own (Godot drops the
	// connections of one that has, when that object goes), whose script object is the loaded program's.
	bool collected() const { return object_ && owner_.is_null() && program_ == Project::generation(); }

	void listens_to(const godot::Signal &p_signal);

	// Whether it is in that signal now: a listener may have been removed since (remove(), a once-only
	// listener that ran, GDScript disconnecting it).
	bool in(const godot::Signal &p_signal) const;

public:
	BoundMethod(vm::Object *p_object, uint32_t p_function, godot::Object *p_owner);
	~BoundMethod() override;

	// The same method of the same object is the same listener.
	static bool equal(const godot::CallableCustom *p_a, const godot::CallableCustom *p_b);
	static bool less(const godot::CallableCustom *p_a, const godot::CallableCustom *p_b);

	uint32_t hash() const override;
	godot::String get_as_text() const override;
	CompareEqualFunc get_compare_equal_func() const override { return equal; }
	CompareLessFunc get_compare_less_func() const override { return less; }
	bool is_valid() const override { return alive(); }
	godot::ObjectID get_object() const override { return owner_; }

	void call(const godot::Variant **p_arguments, int p_argcount, godot::Variant &r_return_value, GDExtensionCallError &r_call_error) const override;
};

} // namespace kas3
