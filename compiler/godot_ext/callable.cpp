#include "callable.h"

#include "any.h"

#include "program.h"

#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_custom.hpp>
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <unordered_map>
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
// The same, by the number the cycle collector knows each by.
std::unordered_map<uint64_t, BoundMethod *> &by_number() {
	static std::unordered_map<uint64_t, BoundMethod *> all;
	return all;
}
class BoundMethod : public CallableCustom {
	friend void kas3::listener_added(const Callable &, const Signal &);
	friend void kas3::release_listeners();
	friend bool kas3::listeners_can_move(vm::Reload &);
	friend void kas3::move_listeners(vm::Interpreter &, vm::Reload &, uint64_t);
	friend void kas3::listeners_of(Object *, std::vector<uint64_t> *);
	friend bool kas3::listener_edges(uint64_t, vm::EngineNode *);
	friend class ListenerRemoval;
	friend void kas3::forget_listeners(const vm::Object *);

	vm::Object *object_;
	// For a class whose Godot object is reference-counted: the callable does not hold the script object, as
	// a callable of Godot's does not hold its object. It goes when that Godot object goes.
	bool weak_ = false;
	uint32_t function_;
	ObjectID owner_; // Null for a class with no Godot object behind it.
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
	std::vector<Signal> signals_;

	// Lets go of the script object. After this the callable refuses to run.
	void let_go() {
		// Releasing the object may free what holds this callable, and so this callable: nothing of it is
		// touched afterwards.
		vm::Object *object = object_;
		object_ = nullptr;
		if (object && !weak_ && Project::generation() == program_) {
			Project::get().interpreter().release_object(object);
		}
	}

	// Whether the script object can still be used: its program is the one loaded, and its owner lives.
	bool alive() const {
		return object_ && Project::generation() == program_ && (owner_.is_null() || ObjectDB::get_instance(owner_) != nullptr);
	}

	// A listener the cycle collector looks through: one with no Godot object of its own (Godot drops the
	// connections of one that has, when that object goes), whose script object is the loaded program's.
	bool collected() const { return object_ && owner_.is_null() && program_ == Project::generation(); }

	void listens_to(const Signal &p_signal) {
		if (std::find(signals_.begin(), signals_.end(), p_signal) != signals_.end()) {
			return;
		}
		signals_.push_back(p_signal);
	}

	// Whether it is in that signal now: a listener may have been removed since (remove(), a once-only
	// listener that ran, GDScript disconnecting it).
	bool in(const Signal &p_signal) const {
		if (!ObjectDB::get_instance(p_signal.get_object_id())) {
			return false;
		}
		Array connections = p_signal.get_connections();
		for (int64_t i = 0; i < connections.size(); i++) {
			Callable callable = Dictionary(connections[i])["callable"];
			if (callable.is_custom() && callable.get_custom() == this) {
				return true;
			}
		}
		return false;
	}

public:
	BoundMethod(vm::Object *p_object, uint32_t p_function, Object *p_owner) :
			object_(p_object), function_(p_function), program_(Project::generation()), key_object_(p_object), key_function_(p_function), key_program_(program_) {
		static uint64_t numbers = 0;
		number_ = ++numbers;
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
		weak_ = p_owner && Project::get().module().classes[p_object->klass].counted;
		if (!weak_) {
			vm::retain(object_);
		}
		bound_methods().insert(this);
		by_number()[number_] = this;
	}
	~BoundMethod() override {
		bound_methods().erase(this);
		by_number().erase(number_);
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
				const size_t at = (size_t)r_call_error.argument + (callee.is_static ? 0 : 1);
				ir::Kind kind = callee.registers[at];
				const std::string &type = at < callee.engine_types.size() ? callee.engine_types[at] : std::string();
				text = "TypeError: Error #1034: Type Coercion failed: cannot convert argument " + std::to_string(r_call_error.argument + 1) +
						" of " + callee.name + "(), a " + given.get_data() + ", to " + (type.empty() ? ir::kind_name(kind) : type.c_str()) + ".";
			} else {
				const size_t first = callee.is_static ? 0 : 1;
				size_t most = callee.parameters > first ? callee.parameters - first : 0;
				size_t least = callee.required > first ? callee.required - first : 0;
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
		bound->listens_to(p_signal);
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
		if (bound->weak_) {
			// It holds neither the old object nor the new one.
			if (bound->object_ && vm::release(bound->object_)) {
				// Its instance keeps it.
			}
			continue;
		}
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

// The cycle collector's view of listeners (vm/collector.h). A listener with no Godot object of its own is
// kept alive by the signals it is in. If it also holds the signal's object, directly or through other
// script objects, and nothing else refers to any of them, they keep each other alive for ever. To the
// collector, such a reference-counted object holds its listeners, one reference for each connection, and a
// listener holds its script object. Godot can't say how many hold a callable: a copy may be kept anywhere
// (GDScript took it from get_connections()). So a listener's count is only of its connections, and before
// a loop is freed each listener is taken out of its signals: if it outlives that, something else holds it,
// and everything is put back as it was.

namespace {

struct TextHash {
	size_t operator()(const String &p_text) const { return p_text.hash(); }
};

// The signals of a class, its own and those it inherits: found once for each class. A signal added to
// one object alone (add_user_signal) is not among them, and a listener in one is not found here: it then
// counts as held from outside, which keeps it, and what it holds.
const std::vector<StringName> &class_signals(const String &p_class) {
	static std::unordered_map<String, std::vector<StringName>, TextHash> known;
	auto [at, made] = known.try_emplace(p_class);
	if (made) {
		TypedArray<Dictionary> signals = ClassDBSingleton::get_singleton()->class_get_signal_list(p_class);
		for (int64_t i = 0; i < signals.size(); i++) {
			at->second.push_back(Dictionary(signals[i])["name"]);
		}
	}
	return at->second;
}

} // namespace

// Read from the object's own connections, so that a copy of a listener GDScript connected is found too.
void listeners_of(Object *p_object, std::vector<uint64_t> *r_nodes) {
	auto look = [&](const StringName &p_name) {
		if (!p_object->has_connections(p_name)) {
			return;
		}
		TypedArray<Dictionary> connections = p_object->get_signal_connection_list(p_name);
		for (int64_t i = 0; i < connections.size(); i++) {
			Callable callable = Dictionary(connections[i])["callable"];
			BoundMethod *bound = callable.is_custom() ? dynamic_cast<BoundMethod *>(callable.get_custom()) : nullptr;
			if (bound && bound->collected()) {
				bound->listens_to(Signal(p_object, p_name));
				r_nodes->push_back(bound->number_);
			}
		}
	};
	for (const StringName &name : class_signals(p_object->get_class())) {
		look(name);
	}
	// The signals its script declares (an AS3 class's [Signal] fields among them).
	const Ref<Script> script = p_object->get_script();
	if (script.is_valid()) {
		TypedArray<Dictionary> declared = script->get_script_signal_list();
		for (int64_t i = 0; i < declared.size(); i++) {
			look(Dictionary(declared[i])["name"]);
		}
	}
}

bool listener_edges(uint64_t p_node, vm::EngineNode *r_node) {
	auto found = by_number().find(p_node);
	if (found == by_number().end() || !found->second->collected()) {
		return false;
	}
	const BoundMethod *bound = found->second;
	r_node->exact = false;
	r_node->references = 0;
	for (const Signal &signal : bound->signals_) {
		r_node->references += bound->in(signal) ? 1 : 0;
	}
	r_node->objects.push_back(bound->object_);
	return true;
}

namespace {

// Listeners taken out of their signals, to see whether anything else holds them, and put back if so.
class ListenerRemoval {
	// One connection of a signal as it was. A listener being taken out is known by its place among those,
	// never by a copy of its callable: a copy would hold it.
	struct Connection {
		Callable callable;
		int64_t flags = 0;
		int taken = -1; // Which of the listeners being taken out it is, or -1.
		int times = 0; // How many times it was connected (a reference-counted connection may be more).
	};
	struct Taken {
		Signal signal;
		std::vector<Connection> connections;
	};

	std::vector<BoundMethod *> listeners_;
	std::vector<uint64_t> numbers_;
	std::vector<Callable> twins_; // Equal to each listener: what takes it out, and what goes back in its place.
	std::vector<Taken> taken_;

	int index_of(const Callable &p_callable) const {
		if (!p_callable.is_custom()) {
			return -1;
		}
		auto found = std::find(listeners_.begin(), listeners_.end(), p_callable.get_custom());
		return found == listeners_.end() ? -1 : static_cast<int>(found - listeners_.begin());
	}

	void remember(const Signal &p_signal) {
		for (const Taken &taken : taken_) {
			if (taken.signal == p_signal) {
				return;
			}
		}
		Taken taken{ p_signal, {} };
		Array connections = p_signal.get_connections();
		for (int64_t i = 0; i < connections.size(); i++) {
			Dictionary connection = connections[i];
			Callable callable = connection["callable"];
			const int index = index_of(callable);
			taken.connections.push_back({ index < 0 ? callable : Callable(), (int64_t)connection["flags"], index, 0 });
		}
		taken_.push_back(std::move(taken));
	}

	// Takes a connection out entirely: how many times it was there.
	static int disconnect(Signal p_signal, const Callable &p_callable) {
		int times = 0;
		while (p_signal.is_connected(p_callable)) {
			p_signal.disconnect(p_callable);
			times++;
		}
		return times;
	}

	// Every connection from the first that was taken out on is taken out and made again, in order: Godot
	// adds a connection at the end.
	void put_back(Taken &p_taken) {
		size_t first = 0;
		while (first < p_taken.connections.size() && p_taken.connections[first].taken < 0) {
			first++;
		}
		for (size_t i = first; i < p_taken.connections.size(); i++) {
			Connection &connection = p_taken.connections[i];
			if (connection.taken < 0) {
				connection.times = disconnect(p_taken.signal, connection.callable);
			}
		}
		for (size_t i = first; i < p_taken.connections.size(); i++) {
			const Connection &connection = p_taken.connections[i];
			const Callable &callable = connection.taken < 0 ? connection.callable : twins_[connection.taken];
			for (int time = 0; time < connection.times; time++) {
				if (p_taken.signal.connect(callable, connection.flags) != OK) {
					UtilityFunctions::push_error("ActionScript: a listener could not be put back in its signal");
				}
			}
			if (connection.taken >= 0 && connection.times > 0) {
				static_cast<BoundMethod *>(callable.get_custom())->listens_to(p_taken.signal);
			}
		}
	}

public:
	// The listeners the collector found held only by their signals and by garbage, and what takes each out.
	explicit ListenerRemoval(const std::vector<uint64_t> &p_nodes) {
		for (uint64_t node : p_nodes) {
			auto found = by_number().find(node);
			if (found != by_number().end() && found->second->collected()) {
				listeners_.push_back(found->second);
				numbers_.push_back(node);
			}
		}
		for (BoundMethod *bound : listeners_) {
			for (const Signal &signal : bound->signals_) {
				if (bound->in(signal)) {
					remember(signal);
				}
			}
		}
		// Made before any is taken out: a bound method made after one is freed could be given its address.
		for (BoundMethod *bound : listeners_) {
			twins_.push_back(Callable(memnew(BoundMethod(bound->object_, bound->function_, nullptr))));
		}
	}

	// Those that outlived being taken out; if there are any, everything is back as it was.
	std::vector<uint64_t> run() {
		for (Taken &taken : taken_) {
			for (Connection &connection : taken.connections) {
				if (connection.taken >= 0) {
					connection.times = disconnect(taken.signal, twins_[connection.taken]);
				}
			}
		}
		std::vector<uint64_t> survivors;
		for (size_t i = 0; i < listeners_.size(); i++) {
			if (bound_methods().count(listeners_[i])) {
				survivors.push_back(numbers_[i]);
			}
		}
		if (!survivors.empty()) {
			for (Taken &taken : taken_) {
				put_back(taken);
			}
		}
		return survivors;
	}
};

} // namespace

std::vector<uint64_t> let_go_listeners(const std::vector<uint64_t> &p_nodes) {
	return ListenerRemoval(p_nodes).run();
}

void forget_listeners(const vm::Object *p_object) {
	for (BoundMethod *bound : bound_methods()) {
		if (bound->weak_ && bound->object_ == p_object) {
			bound->object_ = nullptr;
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
