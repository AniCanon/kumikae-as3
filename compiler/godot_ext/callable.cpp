#include "callable.h"

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

#include <map>
#include <unordered_set>
#include <vector>

using namespace godot;

namespace kas3 {

namespace {

class BoundMethod;
// Every bound method there is, so that the ones a program leaves behind can be dealt with.
std::unordered_set<BoundMethod *> &bound_methods() {
	static std::unordered_set<BoundMethod *> all;
	return all;
}

class BoundMethod : public CallableCustom {
	friend void kas3::listener_added(const Callable &, const Signal &);
	friend void kas3::sweep_listeners();
	friend void kas3::release_listeners();

	vm::Object *object_;
	uint32_t function_;
	ObjectID owner_; // Null for a class with no Godot object behind it.
	uint64_t program_; // Which load of the project's scripts the object belongs to.
	// Set when the one thing done with this callable was to add it to a signal as a listener: the signal.
	Signal listening_to_;
	bool listener_ = false;

	// Lets go of the script object. After this the callable refuses to run.
	void let_go() {
		if (object_ && Project::generation() == program_) {
			vm::Object *object = object_;
			object_ = nullptr;
			Project::get().interpreter().release_object(object);
		}
		object_ = nullptr;
	}

	// Whether the script object can still be used: its program is the one loaded, and its owner lives.
	bool alive() const {
		return object_ && Project::generation() == program_ && (owner_.is_null() || ObjectDB::get_instance(owner_) != nullptr);
	}

public:
	BoundMethod(vm::Object *p_object, uint32_t p_function, Object *p_owner) :
			object_(p_object), function_(p_function), program_(Project::generation()) {
		if (p_owner) {
			owner_ = ObjectID(p_owner->get_instance_id());
		}
		vm::retain(object_);
		bound_methods().insert(this);
	}
	~BoundMethod() override {
		bound_methods().erase(this);
		let_go();
	}

	// The same method of the same object is the same listener.
	static bool equal(const CallableCustom *p_a, const CallableCustom *p_b) {
		const BoundMethod *a = static_cast<const BoundMethod *>(p_a);
		const BoundMethod *b = static_cast<const BoundMethod *>(p_b);
		return a->object_ == b->object_ && a->function_ == b->function_ && a->program_ == b->program_;
	}
	static bool less(const CallableCustom *p_a, const CallableCustom *p_b) {
		const BoundMethod *a = static_cast<const BoundMethod *>(p_a);
		const BoundMethod *b = static_cast<const BoundMethod *>(p_b);
		return a->object_ != b->object_ ? a->object_ < b->object_ : a->function_ < b->function_;
	}

	uint32_t hash() const override {
		uint64_t mixed = (uint64_t)(uintptr_t)object_ * 0x9E3779B97F4A7C15ull + function_;
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
			// AS3's words, and Godot is told all is well.
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
			UtilityFunctions::push_error(String::utf8(text.c_str()));
			r_call_error.error = GDEXTENSION_CALL_OK;
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
// the signal's object, and nothing else refers to either, the two keep each other alive for ever. Found
// here, once a frame: a script object that only its own listeners refer to, each listening to a
// reference-counted object that only that script object's own fields refer to. Removing the listeners
// then frees both. Anything less certain (a callable handed to something other than a signal, an object
// held through another object) is left alone.
void sweep_listeners() {
	if (bound_methods().empty()) {
		return;
	}
	std::map<vm::Object *, std::vector<BoundMethod *>> by_object;
	for (BoundMethod *bound : bound_methods()) {
		if (bound->object_ && bound->owner_.is_null() && bound->program_ == Project::generation()) {
			by_object[bound->object_].push_back(bound);
		}
	}
	if (by_object.empty()) {
		return;
	}
	const ir::Module &module = Project::get().module();
	std::vector<std::pair<Signal, BoundMethod *>> doomed;
	for (auto &[object, bounds] : by_object) {
		if ((size_t)object->references != bounds.size()) {
			continue; // Something else refers to the object.
		}
		bool loop = true;
		for (BoundMethod *bound : bounds) {
			RefCounted *emitter = bound->listener_ ? Object::cast_to<RefCounted>(ObjectDB::get_instance(bound->listening_to_.get_object_id())) : nullptr;
			if (!emitter) {
				loop = false;
				break;
			}
			// How many of the object's own fields refer to the emitter.
			const ir::Class &klass = module.classes[object->klass];
			int held = 0;
			for (size_t i = 0; i < klass.fields.size(); i++) {
				const Variant *value = ir::is_engine(klass.fields[i].kind) ? static_cast<const Variant *>(object->fields()[i].p) : nullptr;
				if (value && value->get_type() == Variant::OBJECT && (uint64_t)(ObjectID)*value == emitter->get_instance_id()) {
					held++;
				}
			}
			if (held == 0 || emitter->get_reference_count() != held) {
				loop = false;
				break;
			}
		}
		if (loop) {
			for (BoundMethod *bound : bounds) {
				doomed.push_back({ bound->listening_to_, bound });
			}
		}
	}
	for (auto &[signal, bound] : doomed) {
		if (!bound_methods().count(bound)) {
			continue;
		}
		Array connections = signal.get_connections();
		for (int64_t i = 0; i < connections.size(); i++) {
			Callable callable = Dictionary(connections[i])["callable"];
			if (callable.is_custom() && callable.get_custom() == bound) {
				connections.clear(); // Its copy of the callable must not outlive the disconnect.
				signal.disconnect(callable);
				break;
			}
		}
	}
}

bool is_script_listener(const Callable &p_callable) {
	return p_callable.is_custom() && dynamic_cast<BoundMethod *>(p_callable.get_custom()) != nullptr;
}

Variant *make_callable(vm::Object *p_object, uint32_t p_function, Object *p_owner) {
	return new Variant(Callable(memnew(BoundMethod(p_object, p_function, p_owner))));
}

} // namespace kas3
