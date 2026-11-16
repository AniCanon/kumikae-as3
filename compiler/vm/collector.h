// The cycle collector: frees objects that refer to each other in a loop, which counting references alone
// never frees.
//
// It deletes by trial (Bacon and Rajan's idea, done as CPython does over a list of every object). Each
// object's count of references is compared with the references that come from the objects looked at. One
// with more is held from outside them: a register, a global, the host, a Godot object that is alive. What
// such an object reaches is in use. The rest is held only by itself, and is freed. Nothing needs to be told
// what the roots are, so a hold the interpreter can't see (a script instance, a listener, a C++ local
// holding a reference) keeps what it holds, as it must.
//
// It works in steps of bounded work, so that a game's frame never waits for a look over everything:
//   - Count: every object on the list, and every engine node it leads to, has the references from each
//     object it is held by taken off its count.
//   - Mark: what has a count left over is in use, and so is what it reaches.
//   - What is left is suspected. The program ran between the steps, so counts read in earlier steps may be
//     out of date: the suspects are counted again in one go, among themselves only, and only those whose
//     every reference comes from another suspect are freed, as drop_object() frees, in that same go. That
//     last look is as long as what it frees, not as long as everything there is.
// An object made while a collection is under way waits for the next one. A class whose objects can hold no
// other object (a vector of numbers) is not looked at at all (vm/held.h).
//
// A loop may pass through the engine: a script object holds a Godot object that has a listener that is a
// method of the script object. The engine's side is asked about such nodes (EngineHandles::node and
// node_edges) and, before a loop through them is freed, to let go of what holds them that it can't count
// (EngineHandles::let_go). A node's joined objects (EngineNode::joined) are in use while it is, and held
// by no reference of its: nothing is taken off their counts.
#pragma once

#include "engine_types.h"
#include "runtime.h"

#include <unordered_map>
#include <vector>

namespace kas3 {
namespace vm {

class Interpreter;

class Collector {
public:
	explicit Collector(Interpreter &p_interpreter);
	// Objects still on the lists are left on none: a host may free them later, through another interpreter.
	~Collector();
	Collector(const Collector &) = delete;
	Collector &operator=(const Collector &) = delete;

	// The list a new object of the class goes on, or null for a class whose objects can't be in a loop.
	Object *list_for(uint32_t p_class) { return p_class < looked_at_.size() && looked_at_[p_class] ? &young_ : nullptr; }
	// Goes on with the collection under way, or starts one, looking at about p_work objects and references.
	// True if a collection ended in this step: what it found has been freed. Nothing if one is running.
	bool step(size_t p_work);
	// Ends the collection under way, then makes a whole one: every loop let go of before now is freed.
	void collect();
	// From now on the engine is not asked about anything: a handle on one of its objects is not looked
	// past, and a node already seen counts as in use.
	void ignore_engine();

private:
	enum class Phase : uint8_t { Idle, Count, Mark };
	// The low two bits of gc_state: what this collection found of the object. The rest is which collection.
	static constexpr uint32_t COUNTED = 0, IN_USE = 1, SUSPECT = 2;
	static constexpr uint32_t EPOCHS = 1u << 30;
	// Asking the engine about a node costs about as much as looking at this many references.
	static constexpr size_t NODE_WORK = 32;
	// An engine node: its count, as an object's gc_count is kept.
	struct Node {
		int64_t count = 0;
		bool in_use = false;
		// What it holds, kept only within one step (asked: which), or while the suspects are counted again.
		EngineNode edges;
		uint32_t asked = 0;
	};

	Interpreter &interpreter_;
	std::vector<uint8_t> looked_at_; // Per class: whether its objects go on the list.
	Object young_{}; // Made since the collection under way started.
	Object all_{}; // The rest; during a collection, those not yet found in use.
	Object cursor_{}; // Walks along all_ while a pass is under way.
	Object gray_{}; // Found in use; what they hold not yet looked at.
	Object done_{}; // Found in use, and what they hold looked at.
	Object suspects_{};
	Phase phase_ = Phase::Idle;
	uint32_t epoch_ = 0;
	bool running_ = false;
	bool engine_ = true;
	size_t work_ = 0;
	uint32_t steps_ = 0;
	std::unordered_map<uint64_t, Node> nodes_;
	std::vector<uint64_t> node_queue_; // Nodes to count, or found in use and not yet followed.
	std::vector<Object *> found_; // While the suspects are counted again: those found in use, or garbage.
	std::vector<uint64_t> kept_; // Nodes the engine could not let go of: in use.

	void begin();
	bool counting(size_t p_work);
	bool marking(size_t p_work);
	void finish();
	Object *advance(); // The next object of all_ for the pass under way, or null at its end.
	void stop_walking();
	bool seen(const Object *p_object) const { return (p_object->gc_state >> 2) == epoch_; }
	uint32_t found(const Object *p_object) const { return seen(p_object) ? p_object->gc_state & 3 : COUNTED; }
	void touch(Object *p_object);
	bool ask(uint64_t p_node, EngineNode *r_edges);
	Node &touch_node(uint64_t p_node);
	void count_node(uint64_t p_node);
	void reach(Object *p_object);
	void reach_node(uint64_t p_node);
	template <typename OnObject, typename OnNode>
	void each_reference(Object *p_object, OnObject &&p_on_object, OnNode &&p_on_node);
	// The last look, over the suspects alone. False, and nothing freed, if the engine kept part of it.
	bool count_suspects();
	Node &suspect_node(uint64_t p_node);
	void rescue();
	bool free_garbage();
};

} // namespace vm
} // namespace kas3
