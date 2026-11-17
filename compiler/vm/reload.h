// Moving a running program onto a newer build of itself: what a game being edited needs.
#pragma once

#include "interpreter.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kas3 {
namespace vm {

// What a reload did for the objects alive whose constructors would give them other values now
// (Reload::update_in_place), by class as the new program names it, each list in order of the names. A class
// may be in both: some of its objects updated, others not.
struct InPlace {
	std::vector<std::string> updated;
	// The class, and why, in words that follow its name ("it extends Godot's Node2D").
	std::vector<std::pair<std::string, std::string>> left;
};

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
	std::vector<uint8_t> missing_classes_; // By class of the old program: an object of it had no place.
	std::vector<std::string> changed_statics_; // What globals() was given: the classes whose static fields' code changed.
	// By class of the new program: an object of it was made with arguments its new constructor does not take.
	std::unordered_set<uint32_t> unfit_;
	// For each object whose record is CARRIED: the class of the old program it was an object of. The old
	// object itself may be gone before update_in_place(): a host lets go of each as it is moved.
	std::unordered_map<Object *, uint32_t> carried_;

	// An object of the class has no place in the new program: said, and null.
	Object *lost(uint32_t p_old_class);

	// What a field or a static field held, as the new one holds it: nothing, if the new one is declared
	// to hold another class of object (or of Godot's, by its name there) than the one that was there.
	Slot carry_into(ir::Kind p_kind, uint32_t p_class, std::string_view p_engine_type, Slot p_value);
	Slot carry(ir::Kind p_kind, Slot p_value);
	void carry_entries(const Entries &p_from, Entries *p_to);
	Object *moved_vector(Vector *p_old, uint32_t p_class);
	void carry_fields(Object *p_old, Object *p_fresh);
	void constants_again(const std::string &p_class);
	// For each field of the new class, the old class's field of its name, or NONE. A base and its subclass
	// may each have a private field of one name: the n-th of a name is the n-th.
	static std::vector<uint32_t> fields_by_name(const ir::Class &p_old, const ir::Class &p_new);
	// What the old object was constructed with (vm/constructions.h), remembered for the new one: the old
	// program keeps its own. Where the old program kept the arguments alone, the new one has them as
	// CARRIED until update_in_place() settles them.
	void carry_record(Object *p_old, Object *p_fresh);
	bool constant_changed(const std::string &p_name) const;
	// A throwaway object of a program's class with its constructor run, given the arguments of a record:
	// the caller's to release. Null if there is no memory or the constructor failed, which is reported as
	// any error of a call from outside is.
	static Object *made_again(Interpreter &p_program, uint32_t p_class, const Constructions::Record &p_record);
	// The first values of an object whose record is CARRIED, as the new program's fields hold them: what
	// the old program's chain gives for the arguments carried (which its constructor takes as the new one
	// does), run again on a throwaway of the old class; for a field that is new or of another kind, what
	// the object holds. Each belongs to the throwaway it came from (`r_made`, the caller's to release with
	// the old program) or to the object. False if the old chain could not be run.
	bool first_values(Object *p_fresh, std::vector<Slot> *r_values, Object **r_made);
	// A CARRIED record given its first values, and so ALL; forgotten if they can't be had.
	void keep_first_values(Object *p_fresh);
	// The objects of one class of the new program: what is still CARRIED of them is settled. Left to the
	// new chain where that gives the same (`p_same`: the chain can be run again, and neither it nor a
	// constant it reads changed), and else given its first values.
	void settle(const std::vector<Object *> &p_objects, bool p_same);
	enum class Again { Updated, NotRemembered, Failed };
	Again construct_again(Object *p_object);

public:
	Reload(Interpreter &p_from, Interpreter &p_to);
	~Reload();
	Reload(const Reload &) = delete;
	Reload &operator=(const Reload &) = delete;

	// The object's counterpart in the new program, with a reference of the caller's own. Null for null,
	// and for an object whose class the new program does not have: missing() then names the class. Met in a
	// field or an untyped value, such an object leaves null there; a host refuses a reload that met one
	// (a library class made on first use, such as Date, counts: a build that no longer uses it lacks it).
	Object *object(Object *p_old);
	// Static fields and package variables, by name. A class whose static fields had been given their
	// values is not given them again; one of its fields that is new starts empty. Of the classes named
	// (those whose static fields' first values changed: ir::static_changes), the constants take the new code's.
	void globals(const std::vector<std::string> &p_changed = {});
	// After globals() and after every object alive has been moved: the objects whose constructors would
	// give them other values now are updated in place. Those are the objects of the classes named (whose
	// code that runs once changed: ir::run_once_changes) and of the classes whose constructor chain reads a
	// constant that has another value now. Where the chain depends only on its arguments and constants
	// (ir::constructor_chain), it is run again with the new code on a throwaway object, with the arguments
	// the object was made with: a field whose first value comes out different, and which still holds the
	// old first value, takes the new one; a field the program has changed since keeps what it holds. The
	// new first values are then the ones remembered, so that the next reload starts from them. The
	// throwaway is freed. A class whose chain can't be run again is left as it is and named with the reason,
	// and so is one whose object was made with something that is not remembered. An object the old
	// program remembered by its arguments alone has its old first values from the old program's chain,
	// run again here: the old program must still be able to run. A reload that ends without this forgets
	// what such objects were made with.
	InPlace update_in_place(const std::vector<std::string> &p_run_once);
	// Where a function of the old program is in the new one, by its name. NONE if it is gone.
	uint32_t function(uint32_t p_old) const;
	const std::string &missing() const { return missing_; }
	// By class of the old program: whether an object of it was met that has no place in the new one.
	const std::vector<uint8_t> &missing_classes() const { return missing_classes_; }
	// The new program's interpreter, for a host that changes what was moved.
	Interpreter &target() { return to_; }
	// Whether an object of one of these classes (named as the new program names them) has been moved so
	// far: one that is alive.
	bool alive(const std::vector<std::string> &p_classes) const;
	// After globals(): whether the class (named as the new program names it) has had its static fields
	// given their values, which it keeps.
	bool statics_given(const std::string &p_class) const;

	// By class of the program, whether an object of it can be reached from the program's static fields or
	// from these objects of its own, through what objects hold (`each_held`). Not through Godot's objects.
	static std::vector<uint8_t> reached(const Interpreter &p_program, const std::vector<Object *> &p_roots);
	// Adds to a new build the classes of the library's containers (a Vector.<T>, Array, Object, Dictionary)
	// that it no longer names but that the running program has objects of (`p_live`, by its classes: those
	// reached(), or a reload's missing_classes()), so that those objects can be moved: an object is moved
	// only to a class of the same name. False if there was none to add.
	static bool keep_containers(const ir::Module &p_old, const std::vector<uint8_t> &p_live, ir::Module *r_new);
};

} // namespace vm
} // namespace kas3
