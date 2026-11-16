// What another file can see of a declaration, as numbers. Equal numbers before and after a change mean that
// no other file can tell the difference; a changed number names the dependency key to check again.
#pragma once

#include "dependencies.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kas3 {

struct Package;
struct Symbol;
struct Unit;

struct Prints {
	uint64_t signature = 0; // The declaration outside any body, with what its written types resolved to.
	uint64_t chain = 0; // For a class: its signature, and the chain of its supertypes.
	uint64_t shape = 0; // For a class: its chain, every member, and the shape of its supertypes.
	const Package *package = nullptr; // Where it is found by name at package level, from any file; null if not.
	bool constructor = false;
};

class Fingerprints {
	std::unordered_map<const Symbol *, Prints> prints_;
	std::unordered_map<const Symbol *, Prints> before_; // This round's earlier prints of what was redone.
	std::unordered_set<const Unit *> round_; // The units redone this round.
	std::unordered_set<const Symbol *> fresh_; // Printed this round.
	std::unordered_set<const Symbol *> visiting_;

	const Prints &class_prints(const Symbol *p_class);
	void emit(const Symbol *p_symbol, const Prints *p_before, const Prints *p_after, std::vector<Dependency> &r_keys) const;

public:
	// Before a unit's declarations are redone: what they were, to compare with afterwards.
	void forget(const std::vector<Symbol *> &p_declared);
	void begin_round(const std::vector<Unit *> &p_units);
	// Prints every declaration of the round's units, and appends the keys whose prints changed, and those
	// of declarations that were added or are gone.
	void finish_round(std::vector<Dependency> &r_keys);
	void drop(const Symbol *p_symbol) { prints_.erase(p_symbol); }
};

} // namespace kas3
