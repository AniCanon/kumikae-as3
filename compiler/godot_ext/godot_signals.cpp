#include "godot_calls.h"

#include "callable.h"
#include "program.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/signal.hpp>

using namespace godot;

namespace kas3 {

// The signal a verb is asked of, or why there is none.
static bool signal_of(const Variant &p_receiver, Signal *r_signal, std::string *r_error) {
	if (p_receiver.get_type() != Variant::SIGNAL) {
		*r_error = "this value is not a signal";
		return false;
	}
	*r_signal = p_receiver;
	if (r_signal->is_null()) {
		*r_error = "the signal is empty: it belongs to no object";
		return false;
	}
	if (!ObjectDB::get_instance(r_signal->get_object_id())) {
		*r_error = FREED;
		return false;
	}
	return true;
}

// Whether the signal is a singleton's (Input's): one the whole game shares.
static bool of_singleton(const Signal &p_signal) {
	Object *object = ObjectDB::get_instance(p_signal.get_object_id());
	Engine *engine = Engine::get_singleton();
	const PackedStringArray names = engine->get_singleton_list();
	for (int64_t i = 0; object && i < names.size(); i++) {
		// The class first: asking for a singleton the game lacks (an editor's) makes Godot complain.
		if (object->is_class(names[i]) && engine->get_singleton(names[i]) == object) {
			return true;
		}
	}
	return false;
}

// Whose code asks: AS3's `this` in the method that runs (none in static code or a function expression,
// where `this` is not a method's object), and the class the code is written in.
struct Caller {
	const vm::Object *object = nullptr;
	uint32_t klass = ir::NONE;
};
static Caller caller() {
	Project &project = Project::get();
	Caller found;
	if (project.native_caller(&found.object, &found.klass)) {
		return found;
	}
	vm::Interpreter &interpreter = project.interpreter();
	if (interpreter.depth() == 0) {
		return found;
	}
	const size_t frame = interpreter.depth() - 1;
	const ir::Function &function = interpreter.function_at(frame);
	found.klass = function.owner != ir::NONE ? function.owner : function.written_in;
	vm::Slot self;
	ir::Kind kind;
	if (function.owner != ir::NONE && !function.is_static && interpreter.local_at(frame, "this", &self, &kind)) {
		found.object = static_cast<const vm::Object *>(self.p);
	}
	return found;
}

// One of Godot's own methods that a script added as a listener (`timeout.add(queueFree)`): Godot's own
// Callable, about which nothing says a script added it. Each is noted here with its signal, by what they
// are to Godot, so that numListeners and removeAll() count it with the script's own methods.
struct Added {
	uint64_t signal_object = 0;
	std::string signal;
	uint64_t object = 0;
	std::string method;

	Added(const Signal &p_signal, const Callable &p_callable) :
			signal_object(p_signal.get_object_id()), signal(String(p_signal.get_name()).utf8().get_data()), object(p_callable.get_object_id()),
			method(String(p_callable.get_method()).utf8().get_data()) {}
	bool operator==(const Added &p_other) const {
		return signal_object == p_other.signal_object && object == p_other.object && signal == p_other.signal && method == p_other.method;
	}
	// Whether the listener is still in its signal: not once either object is freed, or it was removed.
	bool connected() const {
		Object *from = ObjectDB::get_instance(ObjectID(signal_object));
		Object *to = ObjectDB::get_instance(ObjectID(object));
		return from && to && from->is_connected(String::utf8(signal.c_str()), Callable(to, String::utf8(method.c_str())));
	}
};

static std::vector<Added> &added() {
	static std::vector<Added> all;
	return all;
}

static void note_added(const Signal &p_signal, const Callable &p_callable) {
	static size_t sweep_at = 64;
	std::vector<Added> &all = added();
	if (all.size() >= sweep_at) {
		// Those that are gone are forgotten each time the list has doubled: a constant cost for each one noted.
		all.erase(std::remove_if(all.begin(), all.end(), [](const Added &p_each) { return !p_each.connected(); }), all.end());
		sweep_at = std::max<size_t>(64, all.size() * 2);
	}
	all.emplace_back(p_signal, p_callable);
}

static void forget_added(const Signal &p_signal, const Callable &p_callable) {
	std::vector<Added> &all = added();
	all.erase(std::remove(all.begin(), all.end(), Added(p_signal, p_callable)), all.end());
}

// Whether a script added the listener, and for a singleton's signal whether it is the calling object's own:
// a method of the Godot object that object is attached to.
static bool added_by_script(const Signal &p_signal, const Callable &p_callable, bool p_shared, const Caller &p_asking) {
	if (p_callable.is_custom()) {
		return p_shared ? listener_belongs_to(p_callable, p_asking.object, p_asking.klass) : is_script_listener(p_callable);
	}
	const std::vector<Added> &all = added();
	if (!p_callable.is_standard() || std::find(all.begin(), all.end(), Added(p_signal, p_callable)) == all.end()) {
		return false;
	}
	if (!p_shared) {
		return true;
	}
	const ir::Module &module = Project::get().module();
	const bool attached = p_asking.object && !module.classes[p_asking.object->klass].native_base.empty();
	void *owner = attached ? vm::owner_field(const_cast<vm::Object *>(p_asking.object)).p : nullptr;
	return owner && static_cast<Variant *>(owner)->get_validated_object() == p_callable.get_object();
}

// numListeners and removeAll(): the listeners scripts added, counted or taken away. On a singleton's
// signal, which every script shares, only the calling script's own, as C# lets code take away only the
// handlers it holds.
static bool script_listeners(Signal &p_signal, std::string_view p_verb, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) {
	Array connections = p_signal.get_connections();
	const bool shared = of_singleton(p_signal);
	const Caller asking = shared ? caller() : Caller();
	const auto counts = [&](const Callable &p_callable) { return added_by_script(p_signal, p_callable, shared, asking); };
	if (p_verb == "@numListeners") {
		// The ones removeAll() would take away.
		int64_t count = 0;
		for (int64_t i = 0; i < connections.size(); i++) {
			count += counts(Dictionary(connections[i])["callable"]) ? 1 : 0;
		}
		return take(p_result, Variant(count), "numListeners", r_result, r_error);
	}
	// What scripts added, and only that: the engine and GDScript connect listeners of their own (a
	// container listens to its children), which are not AS3's to take away.
	for (int64_t i = 0; i < connections.size(); i++) {
		Callable callable = Dictionary(connections[i])["callable"];
		if (counts(callable) && p_signal.is_connected(callable)) {
			p_signal.disconnect(callable);
			forget_added(p_signal, callable);
		}
	}
	return true;
}

// add() and addOnce(). Adding a listener that is already there does nothing, as with AS3's
// addEventListener. But it can't be there both to stay and for once only: that is an error, as in
// as3-signals.
static bool add_listener(Signal &p_signal, const Callable &p_callable, bool p_connected, std::string_view p_verb, const vm::Argument *p_arguments,
		size_t p_count, std::string *r_error) {
	bool once = p_verb == "@addOnce";
	if (p_connected) {
		Array connections = p_signal.get_connections();
		for (int64_t i = 0; i < connections.size(); i++) {
			Dictionary connection = connections[i];
			if (Callable(connection["callable"]) == p_callable && (((int64_t)connection["flags"] & Object::CONNECT_ONE_SHOT) != 0) != once) {
				*r_error = once ? "this listener was added with add(): remove it before adding it with addOnce()"
								: "this listener was added with addOnce(): remove it before adding it with add()";
				return false;
			}
		}
	}
	if (!p_connected) {
		bool deferred = p_count >= 2 && p_arguments[1].kind == ir::Kind::Bool && p_arguments[1].slot.b;
		int64_t flags = (deferred ? Object::CONNECT_DEFERRED : 0) | (p_verb == "@addOnce" ? Object::CONNECT_ONE_SHOT : 0);
		if (p_signal.connect(p_callable, flags) != OK) {
			*r_error = "Godot could not add the listener";
			return false;
		}
		listener_added(p_callable, p_signal);
		if (p_callable.is_standard()) {
			note_added(p_signal, p_callable);
		}
	}
	return true;
}

// The AS3 way of listening to a signal, made from Godot's own calls. p_verb is "@add" and so on: the
// names the generator gives members that Godot has no one method for.
bool signal_verb(const Variant &p_receiver, std::string_view p_verb, const vm::Argument *p_arguments, size_t p_count,
		const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) {
	Signal signal;
	if (!signal_of(p_receiver, &signal, r_error)) {
		return false;
	}
	if (p_verb == "@removeAll" || p_verb == "@numListeners") {
		return script_listeners(signal, p_verb, p_result, r_result, r_error);
	}
	Variant listener = p_count >= 1 ? to_variant(p_arguments[0].kind, p_arguments[0].slot) : Variant();
	if (listener.get_type() != Variant::CALLABLE) {
		*r_error = "the listener is null";
		return false;
	}
	Callable callable = listener;
	bool connected = signal.is_connected(callable);
	if (p_verb == "@has") {
		return take(p_result, Variant(connected), "has", r_result, r_error);
	}
	if (p_verb == "@remove") {
		if (connected) {
			signal.disconnect(callable);
			forget_added(signal, callable);
		}
		return true;
	}
	if (p_verb == "@add" || p_verb == "@addOnce") {
		return add_listener(signal, callable, connected, p_verb, p_arguments, p_count, r_error);
	}
	*r_error = "Godot has no '" + std::string(p_verb.substr(1)) + "' on a signal";
	return false;
}

} // namespace kas3
