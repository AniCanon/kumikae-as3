// Who holds what, with no frame in sight: giving a value another holder, letting one go, and freeing an
// object whose last reference went with all it holds. The interpreter has one; whatever else runs a
// program over the same values (native code's host) reaches it, and the library works on it (vm/library.h).
#pragma once

#include "clock.h"
#include "collector.h"
#include "engine_handles.h"
#include "engine_source.h"
#include "ir.h"
#include "runtime.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kas3 {
namespace vm {

class Heap {
public:
	const ir::Module &module;
	EngineHandles &handles;
	EngineSource &source;
	Clock &clock; // The host's time and zone, for Date.
	Collector collector; // After the module: it reads the classes.

	Heap(const ir::Module &p_module, EngineHandles &p_handles, EngineSource &p_source, Clock &p_clock = SystemClock::instance());
	Heap(const Heap &) = delete;
	Heap &operator=(const Heap &) = delete;

	// Gives up the reference a slot holds, and empties it.
	void drop(ir::Kind p_kind, Slot &p_slot);
	void drop_any(Slot p_value); // Gives up what an untyped value holds.
	Slot share(ir::Kind p_kind, Slot p_value); // The value with a reference of its own.
	void put(ir::Kind p_kind, Slot &p_target, Slot p_owned); // Stores it, giving up what was there.
	void assign(ir::Kind p_kind, Slot &p_slot, void *p_value); // Takes over one reference.
	void release_object(Object *p_object);
	// Frees an object whose last reference is gone, and whatever that in turn leaves unreferenced.
	void drop_object(Object *p_object);
	// The list a new object of the class is made on (runtime.h, new_object): the collector's, or none.
	Object *list_for(uint32_t p_class) { return collector.list_for(p_class); }
	// The function a class runs for a method of that name, its own or inherited, or NONE: read once from
	// the module, for whatever looks a method up by its name as the program runs.
	uint32_t find_method(uint32_t p_class, std::string_view p_name) const;

private:
	std::vector<std::unordered_map<std::string, uint32_t>> methods_; // Per class: a method's function by name.
	std::vector<Object *> dying_; // Objects whose last reference is gone, still to be freed.
	bool freeing_ = false; // drop_object() is freeing them: one called meanwhile adds to the list.
	void let_go(ir::Kind p_kind, Slot &p_slot); // One slot of an object being freed.
};

} // namespace vm
} // namespace kas3
