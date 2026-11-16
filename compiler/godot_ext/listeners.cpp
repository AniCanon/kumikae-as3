#include "bound_method.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/signal.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <unordered_map>
#include <vector>

using namespace godot;

namespace kas3 {

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

std::vector<uint64_t> let_go_listeners(const std::vector<uint64_t> &p_nodes) {
	return ListenerRemoval(p_nodes).run();
}

} // namespace kas3
