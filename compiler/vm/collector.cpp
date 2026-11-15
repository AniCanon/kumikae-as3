#include "collector.h"

#include "interpreter_internal.h"

#include <limits>

namespace kas3 {
namespace vm {

using ir::Kind;

namespace {

// A count of references as the collector keeps it, which may go below zero while counts are out of date.
int32_t as_count(uint32_t p_references) {
	return p_references > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ? std::numeric_limits<int32_t>::max()
																					 : static_cast<int32_t>(p_references);
}

// A node the engine says is gone has a count no reference can bring down: it is in use.
constexpr int64_t GONE = std::numeric_limits<int64_t>::max() / 2;

// The engine is asked again this many times, at most, when it puts back part of a loop it was asked to let
// go of; after that, everything suspected is kept until the next collection.
constexpr int MOST_TRIES = 8;

void insert_after(Object *p_at, Object *p_object) {
	p_object->gc_previous = p_at;
	p_object->gc_next = p_at->gc_next;
	p_at->gc_next->gc_previous = p_object;
	p_at->gc_next = p_object;
}

} // namespace

Collector::Collector(Interpreter &p_interpreter) :
		interpreter_(p_interpreter) {
	for (Object *head : { &young_, &all_, &gray_, &done_, &suspects_ }) {
		list_init(head);
	}
	const ir::Module &module = interpreter_.module_;
	looked_at_.resize(module.classes.size());
	for (size_t i = 0; i < module.classes.size(); i++) {
		looked_at_[i] = may_hold_objects(module.classes[i]);
	}
}

Collector::~Collector() {
	stop_walking();
	for (Object *head : { &young_, &all_, &gray_, &done_, &suspects_ }) {
		while (!list_empty(head)) {
			list_remove(head->gc_next);
		}
	}
}

// Each counted reference an object holds to an object that is looked at, or to an engine node.
template <typename OnObject, typename OnNode>
void Collector::each_reference(Object *p_object, OnObject &&p_on_object, OnNode &&p_on_node) {
	EngineHandles &engine = interpreter_.host_.handles;
	each_held(interpreter_.module_.classes[p_object->klass], p_object, [&](Kind p_kind, Slot &p_slot) {
		work_++;
		const AnyTag tag = p_kind == Kind::Any ? any::tag(p_slot) : AnyTag::Undefined;
		if (p_kind == Kind::Obj || tag == AnyTag::Object) {
			Object *held = static_cast<Object *>(p_kind == Kind::Obj ? p_slot.p : any::pointer(p_slot));
			if (held && held->gc_next) {
				p_on_object(held);
			}
		} else if (engine_ && ((p_kind == Kind::Ref && p_slot.p) || tag == AnyTag::Godot)) {
			if (const uint64_t node = engine.node(p_kind == Kind::Ref ? p_slot.p : any::pointer(p_slot))) {
				p_on_node(node);
			}
		}
	});
}

bool Collector::step(size_t p_work) {
	if (running_) {
		return false; // Freeing let the engine run the program, which asked for more.
	}
	running_ = true;
	work_ = 0;
	steps_++;
	bool ended = false;
	if (phase_ == Phase::Idle) {
		begin();
	}
	if (phase_ == Phase::Count && counting(p_work)) {
		stop_walking();
		insert_after(&all_, &cursor_);
		for (auto &[key, node] : nodes_) {
			if (node.count > 0) {
				node.in_use = true;
				node_queue_.push_back(key);
			}
		}
		phase_ = Phase::Mark;
	}
	if (phase_ == Phase::Mark && marking(p_work)) {
		finish();
		ended = true;
	}
	running_ = false;
	return ended;
}

void Collector::collect() {
	if (running_) {
		return;
	}
	const size_t all = std::numeric_limits<size_t>::max();
	if (phase_ != Phase::Idle) {
		step(all);
	}
	step(all);
}

void Collector::begin() {
	if (++epoch_ == EPOCHS) {
		// So many collections that the numbers come round again: nothing may look seen by an old one.
		for (Object *head : { &young_, &all_ }) {
			for (Object *at = head->gc_next; at != head; at = at->gc_next) {
				at->gc_state = 0;
			}
		}
		epoch_ = 1;
	}
	list_splice(&all_, &young_);
	nodes_.clear();
	node_queue_.clear();
	insert_after(&all_, &cursor_);
	phase_ = Phase::Count;
}

Object *Collector::advance() {
	Object *next = cursor_.gc_next;
	if (next == &all_) {
		return nullptr;
	}
	list_remove(&cursor_);
	insert_after(next, &cursor_);
	return next;
}

void Collector::stop_walking() {
	list_remove(&cursor_);
}

void Collector::touch(Object *p_object) {
	if (!seen(p_object)) {
		p_object->gc_state = epoch_ << 2 | COUNTED;
		p_object->gc_count = as_count(p_object->references);
	}
}

bool Collector::ask(uint64_t p_node, EngineNode *r_edges) {
	work_ += NODE_WORK;
	return engine_ && interpreter_.host_.handles.node_edges(p_node, r_edges);
}

void Collector::ignore_engine() {
	engine_ = false;
	for (auto &[key, node] : nodes_) {
		node.count = GONE;
		node.edges = EngineNode();
	}
	node_queue_.clear();
}

Collector::Node &Collector::touch_node(uint64_t p_node) {
	auto [at, made] = nodes_.try_emplace(p_node);
	if (made) {
		Node &node = at->second;
		node.count = ask(p_node, &node.edges) ? static_cast<int64_t>(node.edges.references) : GONE;
		node.asked = steps_;
		node_queue_.push_back(p_node);
	}
	return at->second;
}

// The references a node holds come off the counts of what it holds. What it was found to hold is used if
// that was in this step; otherwise it is asked again: what it held then may have been freed since.
void Collector::count_node(uint64_t p_node) {
	EngineNode edges;
	Node &node = nodes_[p_node];
	if (node.asked == steps_) {
		edges = std::move(node.edges);
		node.edges = EngineNode();
	} else if (!ask(p_node, &edges)) {
		return;
	}
	for (Object *object : edges.objects) {
		work_++;
		if (object && object->gc_next) {
			touch(object);
			object->gc_count--;
		}
	}
	for (uint64_t node : edges.nodes) {
		work_++;
		touch_node(node).count--;
	}
}

// True when every object and node has been counted.
bool Collector::counting(size_t p_work) {
	while (work_ < p_work) {
		if (!node_queue_.empty()) {
			const uint64_t node = node_queue_.back();
			node_queue_.pop_back();
			count_node(node);
			continue;
		}
		Object *object = advance();
		if (!object) {
			return true;
		}
		work_++;
		touch(object);
		each_reference(
				object,
				[this](Object *p_held) {
					touch(p_held);
					p_held->gc_count--;
				},
				[this](uint64_t p_node) { touch_node(p_node).count--; });
	}
	return false;
}

void Collector::reach(Object *p_object) {
	if (found(p_object) != IN_USE) {
		p_object->gc_state = epoch_ << 2 | IN_USE;
		list_remove(p_object);
		list_insert(&gray_, p_object);
	}
}

void Collector::reach_node(uint64_t p_node) {
	Node &node = nodes_[p_node];
	if (!node.in_use) {
		node.in_use = true;
		node_queue_.push_back(p_node);
	}
}

// True when everything in use has been found, and what it reaches.
bool Collector::marking(size_t p_work) {
	while (work_ < p_work) {
		if (!list_empty(&gray_)) {
			Object *object = gray_.gc_next;
			list_remove(object);
			list_insert(&done_, object);
			work_++;
			each_reference(object, [this](Object *p_held) { reach(p_held); }, [this](uint64_t p_node) { reach_node(p_node); });
			continue;
		}
		if (!node_queue_.empty()) {
			const uint64_t key = node_queue_.back();
			node_queue_.pop_back();
			EngineNode edges;
			if (ask(key, &edges)) {
				for (Object *object : edges.objects) {
					work_++;
					if (object && object->gc_next) {
						reach(object);
					}
				}
				for (uint64_t node : edges.nodes) {
					work_++;
					reach_node(node);
				}
			}
			continue;
		}
		Object *object = advance();
		if (!object) {
			return true;
		}
		work_++;
		if (object->gc_count > 0) {
			reach(object); // Held from outside what was counted.
		}
	}
	return false;
}

// What was not found in use is suspected, and looked at again in one go.
void Collector::finish() {
	stop_walking();
	list_splice(&suspects_, &all_);
	list_splice(&all_, &done_);
	kept_.clear();
	for (int tries = 0; !list_empty(&suspects_); tries++) {
		if (tries == MOST_TRIES) {
			list_splice(&all_, &suspects_);
			break;
		}
		if (count_suspects()) {
			break;
		}
	}
	nodes_.clear();
	node_queue_.clear();
	kept_.clear();
	phase_ = Phase::Idle;
}

Collector::Node &Collector::suspect_node(uint64_t p_node) {
	auto [at, made] = nodes_.try_emplace(p_node);
	if (made) {
		Node &node = at->second;
		node.count = ask(p_node, &node.edges) ? static_cast<int64_t>(node.edges.references) : GONE;
		node_queue_.push_back(p_node);
	}
	return at->second;
}

// The suspects counted among themselves, with what they hold of the engine's: nothing runs meanwhile, so
// these counts are true. Those that are garbage are then freed. False if the engine kept part of it.
bool Collector::count_suspects() {
	nodes_.clear();
	node_queue_.clear();
	for (Object *object = suspects_.gc_next; object != &suspects_; object = object->gc_next) {
		object->gc_state = epoch_ << 2 | SUSPECT;
		object->gc_count = as_count(object->references);
	}
	for (Object *object = suspects_.gc_next; object != &suspects_; object = object->gc_next) {
		each_reference(
				object,
				[this](Object *p_held) {
					if (found(p_held) == SUSPECT) {
						p_held->gc_count--;
					}
				},
				[this](uint64_t p_node) { suspect_node(p_node).count--; });
	}
	while (!node_queue_.empty()) {
		const uint64_t key = node_queue_.back();
		node_queue_.pop_back();
		for (Object *object : nodes_[key].edges.objects) {
			if (object && object->gc_next && found(object) == SUSPECT) {
				object->gc_count--;
			}
		}
		const std::vector<uint64_t> held = nodes_[key].edges.nodes; // suspect_node() may add to nodes_.
		for (uint64_t node : held) {
			suspect_node(node).count--;
		}
	}
	rescue();
	return free_garbage();
}

// A suspect or node with references from outside the suspects is in use, and so is what it reaches. Those
// objects go back on the list.
void Collector::rescue() {
	found_.clear();
	auto use = [this](Object *p_object) {
		if (found(p_object) == SUSPECT) {
			p_object->gc_state = epoch_ << 2 | IN_USE;
			found_.push_back(p_object);
		}
	};
	auto use_node = [this](uint64_t p_node) {
		auto at = nodes_.find(p_node);
		if (at != nodes_.end() && !at->second.in_use) {
			at->second.in_use = true;
			node_queue_.push_back(p_node);
		}
	};
	for (Object *object = suspects_.gc_next; object != &suspects_; object = object->gc_next) {
		if (object->gc_count > 0) {
			use(object);
		}
	}
	for (auto &[key, node] : nodes_) {
		if (node.count > 0) {
			use_node(key);
		}
	}
	for (uint64_t key : kept_) {
		use_node(key);
	}
	while (!found_.empty() || !node_queue_.empty()) {
		if (!found_.empty()) {
			Object *object = found_.back();
			found_.pop_back();
			each_reference(object, use, use_node);
			continue;
		}
		const uint64_t key = node_queue_.back();
		node_queue_.pop_back();
		const Node &node = nodes_[key];
		for (Object *object : node.edges.objects) {
			if (object && object->gc_next) {
				use(object);
			}
		}
		for (uint64_t held : node.edges.nodes) {
			use_node(held);
		}
	}
	for (Object *object = suspects_.gc_next; object != &suspects_;) {
		Object *next = object->gc_next;
		if (found(object) == IN_USE) {
			list_remove(object);
			list_insert(&all_, object);
		}
		object = next;
	}
}

// Frees what is left among the suspects, as drop_object() frees: each lets go of what it holds once. A
// reference taken on each first keeps every one whole until all are emptied; then the last of each goes.
bool Collector::free_garbage() {
	found_.clear();
	for (Object *object = suspects_.gc_next; object != &suspects_; object = object->gc_next) {
		retain(object);
		found_.push_back(object);
	}
	// Nodes whose count is exact go when the handles on them do; the others must be let go of first.
	std::vector<uint64_t> dead;
	for (const auto &[key, node] : nodes_) {
		if (!node.in_use && !node.edges.exact) {
			dead.push_back(key);
		}
	}
	nodes_.clear(); // The engine may free its nodes now.
	if (found_.empty()) {
		return true;
	}
	if (!dead.empty()) {
		const std::vector<uint64_t> kept = interpreter_.host_.handles.let_go(dead);
		if (!kept.empty()) {
			kept_.insert(kept_.end(), kept.begin(), kept.end());
			const std::vector<Object *> taken = std::move(found_);
			found_.clear();
			for (Object *object : taken) {
				interpreter_.release_object(object);
			}
			return false;
		}
	}
	const std::vector<Object *> garbage = std::move(found_);
	found_.clear();
	for (Object *object : garbage) {
		each_held(interpreter_.module_.classes[object->klass], object, [this](Kind p_kind, Slot &p_slot) {
			Slot value = p_slot;
			p_slot.bits = 0;
			interpreter_.drop(p_kind, value);
		});
	}
	for (Object *object : garbage) {
		// Back on the list first: something the engine ran while they were emptied may hold it still.
		list_remove(object);
		list_insert(&all_, object);
		interpreter_.release_object(object);
	}
	return true;
}

} // namespace vm
} // namespace kas3
