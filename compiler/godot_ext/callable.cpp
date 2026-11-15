#include "callable.h"

#include "any.h"

#include "program.h"

#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_custom.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <map>
#include <unordered_set>
#include <vector>

using namespace godot;

namespace kas3 {

namespace {

class BoundMethod;
class ListenerSweep;
// Every bound method there is, so that the ones a program leaves behind can be dealt with.
std::unordered_set<BoundMethod *> &bound_methods() {
	static std::unordered_set<BoundMethod *> all;
	return all;
}

class BoundMethod : public CallableCustom {
	friend void kas3::listener_added(const Callable &, const Signal &);
	friend void kas3::sweep_listeners();
	friend class ListenerSweep;
	friend void kas3::release_listeners();
	friend bool kas3::listeners_can_move(vm::Reload &);
	friend void kas3::move_listeners(vm::Interpreter &, vm::Reload &, uint64_t);

	vm::Object *object_;
	uint32_t function_;
	ObjectID owner_; // Null for a class with no Godot object behind it.
	uint64_t program_; // Which load of the project's scripts the object belongs to.
	// What makes two of these the same listener, and what Godot files them under. It must not change
	// while a signal holds the callable, so it is not the object and function above: those are replaced
	// when the scripts are reloaded. It is what they were when the method was first bound.
	const vm::Object *key_object_;
	uint32_t key_function_;
	uint64_t key_program_;
	// Set when the one thing done with this callable was to add it to a signal as a listener: the signal.
	Signal listening_to_;
	bool listener_ = false;
	// When a copy of a listener is held outside its signal, the signal holds a stand-in equal to it.
	BoundMethod *stand_in_ = nullptr; // In the copy: the one in the signal.
	BoundMethod *stands_for_ = nullptr; // In the one in the signal: the copy.

	// Lets go of the script object. After this the callable refuses to run.
	void let_go() {
		// Releasing the object may free what holds this callable, and so this callable: nothing of it is
		// touched afterwards.
		vm::Object *object = object_;
		object_ = nullptr;
		if (object && Project::generation() == program_) {
			Project::get().interpreter().release_object(object);
		}
	}

	// Whether the script object can still be used: its program is the one loaded, and its owner lives.
	bool alive() const {
		return object_ && Project::generation() == program_ && (owner_.is_null() || ObjectDB::get_instance(owner_) != nullptr);
	}

public:
	BoundMethod(vm::Object *p_object, uint32_t p_function, Object *p_owner) :
			object_(p_object), function_(p_function), program_(Project::generation()), key_object_(p_object), key_function_(p_function), key_program_(program_) {
		if (p_owner) {
			owner_ = ObjectID(p_owner->get_instance_id());
		}
		// The same method bound before a reload is the same listener: it is found by what it is now.
		for (const BoundMethod *other : bound_methods()) {
			if (other->object_ == object_ && other->function_ == function_ && other->program_ == program_) {
				key_object_ = other->key_object_;
				key_function_ = other->key_function_;
				key_program_ = other->key_program_;
				break;
			}
		}
		vm::retain(object_);
		bound_methods().insert(this);
	}
	~BoundMethod() override {
		bound_methods().erase(this);
		if (stand_in_ && bound_methods().count(stand_in_)) {
			// The copy held outside the signal is gone: the one in the signal is an ordinary listener again.
			stand_in_->stands_for_ = nullptr;
			stand_in_->listener_ = true;
		}
		if (stands_for_ && bound_methods().count(stands_for_)) {
			stands_for_->stand_in_ = nullptr;
		}
		let_go();
	}

	// The same method of the same object is the same listener.
	static bool equal(const CallableCustom *p_a, const CallableCustom *p_b) {
		const BoundMethod *a = static_cast<const BoundMethod *>(p_a);
		const BoundMethod *b = static_cast<const BoundMethod *>(p_b);
		return a->key_object_ == b->key_object_ && a->key_function_ == b->key_function_ && a->key_program_ == b->key_program_;
	}
	static bool less(const CallableCustom *p_a, const CallableCustom *p_b) {
		const BoundMethod *a = static_cast<const BoundMethod *>(p_a);
		const BoundMethod *b = static_cast<const BoundMethod *>(p_b);
		return a->key_object_ != b->key_object_ ? a->key_object_ < b->key_object_ : a->key_function_ < b->key_function_;
	}

	uint32_t hash() const override {
		uint64_t mixed = (uint64_t)(uintptr_t)key_object_ * 0x9E3779B97F4A7C15ull + key_function_;
		return (uint32_t)(mixed ^ (mixed >> 32));
	}
	String get_as_text() const override {
		if (!object_ || Project::generation() != program_) {
			return String("<unloaded ActionScript method>");
		}
		return String::utf8(Project::get().module().functions[function_].name.c_str());
	}
	CompareEqualFunc get_compare_equal_func() const override { return equal; }
	CompareLessFunc get_compare_less_func() const override { return less; }
	bool is_valid() const override { return alive(); }
	ObjectID get_object() const override { return owner_; }

	void call(const Variant **p_arguments, int p_argcount, Variant &r_return_value, GDExtensionCallError &r_call_error) const override {
		if (!on_main_thread()) {
			refuse_thread(function_);
			r_call_error.error = GDEXTENSION_CALL_OK;
			return;
		}
		if (!alive()) {
			// The tombstone: the object this method belongs to is gone.
			r_call_error.error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
			return;
		}
		// The call may drop the last other reference to this callable (a listener that removes itself).
		vm::Object *object = object_;
		const uint64_t program = program_;
		vm::retain(object);
		const uint32_t function = function_;
		call_script(object, function, p_arguments, p_argcount, &r_return_value, &r_call_error);
		if (Project::generation() != program) {
			return;
		}
		if (r_call_error.error != GDEXTENSION_CALL_OK) {
			// The listener was not called: what it was given does not fit its parameters. Godot's own
			// report can't name the method of a callable like this one, so the report is made here, in
			// AS3's words.
			const ir::Function &callee = Project::get().module().functions[function];
			std::string text;
			if (r_call_error.error == GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT && r_call_error.argument >= 0 && r_call_error.argument < p_argcount) {
				CharString given = Variant::get_type_name(p_arguments[r_call_error.argument]->get_type()).utf8();
				ir::Kind kind = callee.registers[(size_t)r_call_error.argument + 1];
				const std::string &type = (size_t)r_call_error.argument + 1 < callee.engine_types.size() ? callee.engine_types[(size_t)r_call_error.argument + 1] : std::string();
				text = "TypeError: Error #1034: Type Coercion failed: cannot convert argument " + std::to_string(r_call_error.argument + 1) +
						" of " + callee.name + "(), a " + given.get_data() + ", to " + (type.empty() ? ir::kind_name(kind) : type.c_str()) + ".";
			} else {
				size_t most = callee.parameters > 0 ? callee.parameters - 1 : 0;
				size_t least = callee.required > 0 ? callee.required - 1 : 0;
				text = "ArgumentError: Error #1063: Argument count mismatch on " + callee.name + "(). Expected " +
						std::to_string((size_t)p_argcount > most ? most : least) + ", got " + std::to_string(p_argcount) + ".";
			}
			// The listener was never entered, so there is no stack: say where it is declared.
			const ir::Module &module = Project::get().module();
			// Reported in AS3's words, and Godot is told the call failed, so that whatever made it (a
			// signal, callv, filter) knows.
			if (callee.file < module.files.size() && callee.file < module.lines.size()) {
				const std::vector<uint32_t> &starts = module.lines[callee.file];
				size_t line = 0;
				while (line < starts.size() && starts[line] <= callee.position) {
					line++;
				}
				text += "\n\tat " + callee.name + "()[" + module.files[callee.file] + ":" + std::to_string(line) + "]";
				Project::get().host().error_at(text, callee.name + "()", module.files[callee.file], (int32_t)line);
			} else {
				Project::get().host().error(text);
			}
		}
		Project::get().interpreter().release_object(object);
	}
};

} // namespace

void listener_added(const Callable &p_callable, const Signal &p_signal) {
	if (BoundMethod *bound = p_callable.is_custom() ? dynamic_cast<BoundMethod *>(p_callable.get_custom()) : nullptr) {
		bound->listening_to_ = p_signal;
		bound->listener_ = true;
	}
}

bool listeners_can_move(vm::Reload &p_reload) {
	const uint64_t current = Project::generation();
	for (const BoundMethod *bound : bound_methods()) {
		if (bound->object_ && bound->program_ == current) {
			vm::Object *moved = p_reload.object(bound->object_);
			if (!moved) {
				return false;
			}
			vm::release(moved); // The reload keeps its own until it is done.
		}
	}
	return true;
}

void move_listeners(vm::Interpreter &p_old, vm::Reload &p_reload, uint64_t p_generation) {
	const uint64_t current = Project::generation();
	const std::vector<BoundMethod *> all(bound_methods().begin(), bound_methods().end());
	for (BoundMethod *bound : all) {
		if (!bound_methods().count(bound) || !bound->object_ || bound->program_ != current) {
			continue;
		}
		vm::Object *old = bound->object_;
		const uint32_t function = p_reload.function(bound->function_);
		// A method that is gone from the new scripts: the listener stays where it is and refuses to run.
		bound->object_ = function == ir::NONE ? nullptr : p_reload.object(old);
		bound->function_ = function == ir::NONE ? bound->function_ : function;
		bound->program_ = p_generation;
		p_old.release_object(old); // May destroy other bound methods: hence the check above.
	}
}

void release_listeners() {
	std::vector<BoundMethod *> all(bound_methods().begin(), bound_methods().end());
	for (BoundMethod *bound : all) {
		// Letting go of one object may destroy other bound methods.
		if (bound_methods().count(bound)) {
			bound->let_go();
		}
	}
}

// A listener with no Godot object of its own is kept alive by the signal it listens to. If it also holds
// the signal's object, directly or through other script objects, and nothing else refers to any of them,
// they keep each other alive for ever. Found here, once a frame, by counting.
//
// The things looked at are the script objects that such listeners belong to, whatever those reach through
// their fields, and the reference-counted Godot objects among that. Each has a count of references. A
// reference from a field of one of them, or from a listener on a signal of one of them, is accounted
// for. A thing with more references than are accounted for is held from outside (a variable, a node's
// script, a callable handed to something other than a signal, a collection): it is in use, and so is
// everything it reaches. What is left is held only by itself. Its listeners are removed and its fields
// emptied, which frees it.
namespace {

// One such look.
class ListenerSweep {
	struct Thing {
		int64_t references = 0; // What it says of itself.
		int64_t accounted = 0; // Those that come from other things.
		bool in_use = false;
		std::vector<void *> reaches;
		RefCounted *emitter = nullptr; // Null for a script object.
	};
	// A listener taken out of its signal through a callable equal to it, not through a copy of it, so that
	// it can be seen whether the signal was the only thing holding it.
	struct Removed {
		Signal signal;
		Callable stand_in;
		int64_t flags;
		// Not null if the listener outlived its removal: something else holds a copy of it. (Whether it
		// did is settled at once: a stand-in made later may be given the address of one that is gone.)
		BoundMethod *survivor;
	};

	const ir::Module &module_ = Project::get().module();
	vm::Interpreter &interpreter_ = Project::get().interpreter();
	std::map<void *, Thing> things_;
	std::vector<void *> pending_;
	// The listeners on a signal of each Godot object, as far as they are known.
	std::map<uint64_t, std::vector<BoundMethod *>> listeners_;

	static bool current(const BoundMethod *p_bound) { return p_bound->program_ == Project::generation(); }

	// A listener counts as one only while it really is in the signal it was added to. One that has been
	// removed since (remove(), a once-only listener that ran, GDScript disconnecting it) and is still
	// alive is held by something else, and is a reference from outside like any other.
	static void check_connections() {
		for (BoundMethod *bound : bound_methods()) {
			if (!bound->listener_ || !current(bound)) {
				continue;
			}
			bool connected = false;
			if (ObjectDB::get_instance(bound->listening_to_.get_object_id())) {
				Array connections = bound->listening_to_.get_connections();
				for (int64_t i = 0; i < connections.size() && !connected; i++) {
					Callable callable = Dictionary(connections[i])["callable"];
					connected = callable.is_custom() && callable.get_custom() == bound;
				}
			}
			bound->listener_ = connected;
		}
	}

	void visit(void *p_key, RefCounted *p_emitter) {
		if (things_.count(p_key)) {
			return;
		}
		Thing &thing = things_[p_key];
		thing.emitter = p_emitter;
		thing.references = p_emitter ? p_emitter->get_reference_count() : (int64_t) static_cast<vm::Object *>(p_key)->references;
		pending_.push_back(p_key);
	}

	// A Godot object reaches the script objects that listen to it.
	std::vector<void *> reached_from_emitter(RefCounted *p_emitter) {
		std::vector<void *> reaches;
		auto found = listeners_.find(p_emitter->get_instance_id());
		if (found != listeners_.end()) {
			for (BoundMethod *bound : found->second) {
				visit(bound->object_, nullptr);
				reaches.push_back(bound->object_);
			}
		}
		return reaches;
	}

	// A script object reaches what its fields hold: other script objects, and counted Godot objects.
	std::vector<void *> reached_from_object(vm::Object *p_object) {
		std::vector<void *> reaches;
		const ir::Class &klass = module_.classes[p_object->klass];
		for (size_t i = 0; i < klass.fields.size(); i++) {
			void *held = p_object->fields()[i].p;
			ir::Kind kind = klass.fields[i].kind;
			if (kind == ir::Kind::Any) {
				// What an untyped field holds is walked as a field of that kind would be.
				const vm::Slot value = p_object->fields()[i];
				const vm::AnyTag tag = vm::any::tag(value);
				held = tag == vm::AnyTag::Object || vm::any::is_engine(value) ? vm::any::pointer(value) : nullptr;
				kind = tag == vm::AnyTag::Object ? ir::Kind::Obj : ir::Kind::Ref;
			}
			if (!held) {
				continue;
			}
			if (kind == ir::Kind::Obj) {
				visit(held, nullptr);
				reaches.push_back(held);
			} else if (ir::is_engine(kind)) {
				const Variant *value = static_cast<const Variant *>(held);
				RefCounted *counted = value->get_type() == Variant::OBJECT ? Object::cast_to<RefCounted>(value->get_validated_object()) : nullptr;
				// The script's own hold on the object it is attached to is not a counted reference.
				if (counted && !(i == 0 && !klass.native_base.empty())) {
					visit(counted, counted);
					reaches.push_back(counted);
				}
			}
		}
		return reaches;
	}

	// Everything the listeners without a Godot object of their own can reach, and what each reaches.
	void gather() {
		for (BoundMethod *bound : bound_methods()) {
			if (bound->object_ && bound->listener_ && current(bound)) {
				listeners_[(uint64_t)bound->listening_to_.get_object_id()].push_back(bound);
			}
		}
		for (BoundMethod *bound : bound_methods()) {
			if (bound->object_ && bound->owner_.is_null() && current(bound)) {
				visit(bound->object_, nullptr);
			}
		}
		while (!pending_.empty()) {
			void *key = pending_.back();
			pending_.pop_back();
			RefCounted *emitter = things_[key].emitter;
			std::vector<void *> reaches = emitter ? reached_from_emitter(emitter) : reached_from_object(static_cast<vm::Object *>(key));
			things_[key].reaches = std::move(reaches);
		}
	}

	// A thing with more references than the things gathered account for is held from outside, and so is
	// everything it reaches.
	void mark_in_use() {
		for (auto &[key, thing] : things_) {
			for (void *reached : thing.reaches) {
				things_[reached].accounted++;
			}
		}
		for (auto &[key, thing] : things_) {
			if (thing.references > thing.accounted && !thing.in_use) {
				thing.in_use = true;
				pending_.push_back(key);
			}
		}
		while (!pending_.empty()) {
			void *key = pending_.back();
			pending_.pop_back();
			for (void *reached : things_[key].reaches) {
				if (!things_[reached].in_use) {
					things_[reached].in_use = true;
					pending_.push_back(reached);
				}
			}
		}
	}

	// The script objects nothing holds, and the listeners that go with them.
	void find_unused(std::vector<vm::Object *> &r_dead, std::vector<BoundMethod *> &r_doomed) {
		for (auto &[key, thing] : things_) {
			if (thing.in_use) {
				continue;
			}
			if (!thing.emitter) {
				r_dead.push_back(static_cast<vm::Object *>(key));
				continue;
			}
			for (BoundMethod *bound : listeners_[thing.emitter->get_instance_id()]) {
				// A listener that is itself in use goes when the object it listens to does.
				if (!things_[bound->object_].in_use) {
					r_doomed.push_back(bound);
				}
			}
		}
	}

	static int64_t connection_flags(const Signal &p_signal, const Callable &p_callable) {
		int64_t flags = 0;
		Array connections = p_signal.get_connections();
		for (int64_t i = 0; i < connections.size(); i++) {
			Dictionary connection = connections[i];
			if (Callable(connection["callable"]) == p_callable) {
				flags = connection["flags"];
			}
		}
		return flags;
	}

	// Takes each listener out of its signal. False, and no further, as soon as one outlives its removal:
	// a copy is held somewhere (GDScript kept it in a variable) and may yet be called.
	static bool remove(const std::vector<BoundMethod *> &p_doomed, std::vector<Removed> &r_removed) {
		for (BoundMethod *bound : p_doomed) {
			if (!bound_methods().count(bound)) {
				continue;
			}
			Signal signal = bound->listening_to_;
			BoundMethod *twin = memnew(BoundMethod(bound->object_, bound->function_, nullptr));
			Callable stand_in(twin);
			const int64_t flags = connection_flags(signal, stand_in);
			if (!signal.is_connected(stand_in)) {
				continue;
			}
			signal.disconnect(stand_in);
			const bool survived = bound_methods().count(bound) != 0;
			r_removed.push_back({ signal, stand_in, flags, survived ? bound : nullptr });
			if (survived) {
				return false;
			}
		}
		return true;
	}

	// Everything is put back as it was.
	static void put_back(std::vector<Removed> &p_removed) {
		for (Removed &entry : p_removed) {
			entry.signal.connect(entry.stand_in, entry.flags);
			BoundMethod *twin = static_cast<BoundMethod *>(entry.stand_in.get_custom());
			twin->listening_to_ = entry.signal;
			if (!entry.survivor) {
				twin->listener_ = true;
				continue;
			}
			// The copy that is held elsewhere now stands outside the signal: while it lives, the listener
			// in the signal counts as held from outside. When it goes, that ends.
			twin->listener_ = false;
			twin->stands_for_ = entry.survivor;
			entry.survivor->stand_in_ = twin;
			entry.survivor->listener_ = false;
		}
	}

	void empty_fields(const std::vector<vm::Object *> &p_dead) {
		for (vm::Object *object : p_dead) {
			const ir::Class &klass = module_.classes[object->klass];
			for (size_t i = 0; i < klass.fields.size(); i++) {
				vm::Slot value = object->fields()[i];
				object->fields()[i].bits = 0;
				interpreter_.discard(klass.fields[i].kind, value);
			}
		}
	}

public:
	void run() {
		check_connections();
		gather();
		mark_in_use();
		std::vector<vm::Object *> dead;
		std::vector<BoundMethod *> doomed;
		find_unused(dead, doomed);
		if (dead.empty()) {
			return;
		}
		// Held while they are taken apart, so that nothing is freed half way.
		for (vm::Object *object : dead) {
			vm::retain(object);
		}
		std::vector<Removed> removed;
		if (remove(doomed, removed)) {
			removed.clear();
			empty_fields(dead);
		} else {
			put_back(removed);
		}
		for (vm::Object *object : dead) {
			interpreter_.release_object(object);
		}
	}
};

} // namespace

void sweep_listeners() {
	bool any = false;
	for (BoundMethod *bound : bound_methods()) {
		any = any || (bound->object_ && bound->owner_.is_null() && bound->program_ == Project::generation());
	}
	if (!any) {
		return;
	}
	// The look takes time in proportion to what the listeners reach, so it is paced by its own cost: at
	// most four times a second, and never more than about a fiftieth of the time.
	using Clock = std::chrono::steady_clock;
	static Clock::time_point next;
	const Clock::time_point started = Clock::now();
	if (started < next) {
		return;
	}
	ListenerSweep().run();
	const Clock::duration took = Clock::now() - started;
	next = Clock::now() + std::max<Clock::duration>(std::chrono::milliseconds(250), took * 50);
}

bool is_script_listener(const Callable &p_callable) {
	return p_callable.is_custom() && dynamic_cast<BoundMethod *>(p_callable.get_custom()) != nullptr;
}

Variant *make_callable(vm::Object *p_object, uint32_t p_function, Object *p_owner) {
	return new Variant(Callable(memnew(BoundMethod(p_object, p_function, p_owner))));
}

} // namespace kas3
