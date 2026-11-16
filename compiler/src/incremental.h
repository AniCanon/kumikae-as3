// What an incremental Compilation keeps between edits (Compilation::prepare and update, names_update.cpp).
#pragma once

#include "dependencies.h"
#include "fingerprints.h"
#include "names.h"

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace kas3 {

struct Tracking {
	// A declaration as a redo of its file finds it again: the same file, the same class (or none), kind and
	// name. The n-th one written is matched to the n-th one there was. Keyed by the name's hash, so that
	// nothing is allocated; the symbols under a key are told apart by their names.
	struct Slot {
		const Unit *unit;
		const Symbol *owner;
		SymbolKind kind;
		uint64_t name;
		bool operator==(const Slot &p_other) const {
			return unit == p_other.unit && owner == p_other.owner && kind == p_other.kind && name == p_other.name;
		}
	};
	struct SlotHash {
		size_t operator()(const Slot &p_slot) const noexcept {
			return static_cast<size_t>(p_slot.name ^ (reinterpret_cast<uintptr_t>(p_slot.unit) * 31) ^
					(reinterpret_cast<uintptr_t>(p_slot.owner) * 17) ^ static_cast<uint64_t>(p_slot.kind));
		}
	};
	std::unordered_map<Slot, std::vector<Symbol *>, SlotHash> retracted; // Waiting to be declared again.
	std::vector<Symbol *> unused; // Declarations that are gone; their storage is used again.
	std::vector<ClassInfo *> unused_classes; // The same for what a class that is gone had.
	std::unordered_map<const Unit *, std::shared_ptr<Parsed>> replacing; // New text, taken by the next retract.
	std::vector<std::shared_ptr<Parsed>> retired; // Old text that symbols may still point into, this update.
	Fingerprints prints;
	DependencyIndex index;
	std::set<uint32_t> unchecked; // The ranks of the files whose bodies wait.
	Compilation::Counts counts;
};

} // namespace kas3
