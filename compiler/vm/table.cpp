#include "table.h"

#include "any.h"

#include <new>

namespace kas3 {
namespace vm {

namespace {

std::string_view text_of(Slot p_key) {
	return static_cast<const Str *>(any::pointer(p_key))->view();
}

} // namespace

std::optional<uint32_t> Entries::find_text(std::string_view p_text) const {
	const auto found = texts_.find(p_text);
	return found == texts_.end() ? std::nullopt : std::optional<uint32_t>(found->second);
}

std::optional<uint32_t> Entries::find_object(const void *p_object) const {
	const auto found = objects_.find(p_object);
	return found == objects_.end() ? std::nullopt : std::optional<uint32_t>(found->second);
}

void Entries::add(Slot p_key, Slot p_value) {
	// Packed when more than half are deleted: a table used as a queue stays small.
	if (entries_.size() - live_ > 8 && entries_.size() - live_ > live_) {
		pack();
	}
	const uint32_t at = static_cast<uint32_t>(entries_.size());
	entries_.push_back({ p_key, p_value });
	if (any::tag(p_key) == AnyTag::String) {
		texts_.emplace(text_of(p_key), at);
	} else {
		objects_.emplace(any::pointer(p_key), at);
	}
	live_++;
}

Entry Entries::remove(uint32_t p_at) {
	Entry gone = entries_[p_at];
	if (any::tag(gone.key) == AnyTag::String) {
		texts_.erase(text_of(gone.key));
	} else {
		objects_.erase(any::pointer(gone.key));
	}
	entries_[p_at] = { any::undefined(), any::undefined() };
	live_--;
	return gone;
}

std::vector<Entry> Entries::take_all() {
	std::vector<Entry> taken;
	taken.reserve(live_);
	for (const Entry &entry : entries_) {
		if (any::tag(entry.key) != AnyTag::Undefined) {
			taken.push_back(entry);
		}
	}
	entries_.clear();
	texts_.clear();
	objects_.clear();
	live_ = 0;
	return taken;
}

// The deleted entries taken out, the order kept, and every position found again.
void Entries::pack() {
	std::vector<Entry> kept;
	kept.reserve(live_);
	texts_.clear();
	objects_.clear();
	for (const Entry &entry : entries_) {
		if (any::tag(entry.key) == AnyTag::Undefined) {
			continue;
		}
		const uint32_t at = static_cast<uint32_t>(kept.size());
		if (any::tag(entry.key) == AnyTag::String) {
			texts_.emplace(text_of(entry.key), at);
		} else {
			objects_.emplace(any::pointer(entry.key), at);
		}
		kept.push_back(entry);
	}
	entries_.swap(kept);
}

Object *new_table(uint32_t p_class) {
	Entries *entries = allocation_fails() ? nullptr : new (std::nothrow) Entries();
	if (!entries) {
		return nullptr;
	}
	Object *table = new_object(p_class, 1);
	if (!table) {
		delete entries;
		return nullptr;
	}
	table->fields()[0].p = entries;
	return table;
}

void free_table(Object *p_table) {
	delete entries_of(p_table);
	free_object(p_table);
}

Entries *properties_of(Vector *p_array) {
	if (!p_array->properties) {
		p_array->properties = allocation_fails() ? nullptr : new (std::nothrow) Entries();
	}
	return p_array->properties;
}

void free_properties(Vector *p_array) {
	delete p_array->properties;
	p_array->properties = nullptr;
}

} // namespace vm
} // namespace kas3
