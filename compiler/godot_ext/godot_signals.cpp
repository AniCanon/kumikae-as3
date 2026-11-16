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
	vm::Interpreter &interpreter = Project::get().interpreter();
	Caller found;
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

// numListeners and removeAll(): the listeners scripts added, counted or taken away. On a singleton's
// signal, which every script shares, only the calling script's own, as C# lets code take away only the
// handlers it holds.
static bool script_listeners(Signal &p_signal, std::string_view p_verb, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) {
	Array connections = p_signal.get_connections();
	const bool shared = of_singleton(p_signal);
	const Caller asking = shared ? caller() : Caller();
	const auto counts = [&](const Callable &p_callable) {
		return shared ? listener_belongs_to(p_callable, asking.object, asking.klass) : is_script_listener(p_callable);
	};
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
