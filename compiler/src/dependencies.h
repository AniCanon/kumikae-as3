// What each file's check read from other files, so that a change to a declaration rechecks only the files
// that read it. A dependency is a key; each file has two lists of keys (what its declarations read, and what
// its bodies read), and the index finds the files whose current lists hold a key.
#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kas3 {

struct Symbol;
struct Unit;

enum class DependencyKind : uint8_t {
	Symbol, // A declaration found by name: its signature.
	Chain, // A class used as a type: its header and the chain of its supertypes.
	Shape, // A class extended or implemented: its chain and every member, its supertypes' too.
	Member, // A member name looked up through a program class, found or not.
	Name, // A package-level name looked up or declared, found or not.
	Defines, // A package-level name this file defines too: which file's definition is first, and how it is said.
	Package, // A package imported whole: whether it holds anything.
	Positions, // Where a file's declarations are: another file's message gives one.
	Halves, // A file's property halves, which a subclass in another file took.
};

struct Dependency {
	DependencyKind kind = DependencyKind::Name;
	const void *subject = nullptr; // The symbol, class or unit; null for a name or a package.
	uint64_t name = 0; // The hash of a name, for Member, Name and Package.

	bool operator==(const Dependency &p_other) const {
		return kind == p_other.kind && subject == p_other.subject && name == p_other.name;
	}
	bool operator<(const Dependency &p_other) const;
};

struct DependencyHash {
	size_t operator()(const Dependency &p_key) const noexcept;
};

uint64_t name_hash(std::string_view p_name);

inline Dependency on_symbol(const Symbol *p_symbol) { return { DependencyKind::Symbol, p_symbol, 0 }; }
inline Dependency on_chain(const Symbol *p_class) { return { DependencyKind::Chain, p_class, 0 }; }
inline Dependency on_shape(const Symbol *p_class) { return { DependencyKind::Shape, p_class, 0 }; }
inline Dependency on_member(const Symbol *p_class, std::string_view p_name) {
	return { DependencyKind::Member, p_class, name_hash(p_name) };
}
inline Dependency on_name(std::string_view p_name) { return { DependencyKind::Name, nullptr, name_hash(p_name) }; }
inline Dependency on_defines(std::string_view p_name) { return { DependencyKind::Defines, nullptr, name_hash(p_name) }; }
inline Dependency on_package(std::string_view p_name) { return { DependencyKind::Package, nullptr, name_hash(p_name) }; }
inline Dependency on_positions(const Unit *p_unit) { return { DependencyKind::Positions, p_unit, 0 }; }
inline Dependency on_halves(const Unit *p_unit) { return { DependencyKind::Halves, p_unit, 0 }; }

class DependencyIndex {
public:
	enum Part : uint8_t { Declarations, Bodies };

private:
	struct Reader {
		uint32_t unit; // Its rank.
		uint32_t epoch; // The list it came from: stale once the unit's list for that part is replaced.
		Part part;
	};
	std::unordered_map<Dependency, std::vector<Reader>, DependencyHash> readers_;
	std::vector<uint32_t> epochs_[2]; // By rank, for each part.
	size_t entries_ = 0;
	size_t live_ = 0;
	std::vector<size_t> counts_[2]; // How many keys each unit's current list has.

	void compact();

public:
	// Replaces what one part of a unit depends on.
	void set(uint32_t p_unit, Part p_part, std::vector<Dependency> p_keys);
	// The units whose current lists hold the key, added to r_units (which may then hold a unit twice).
	void readers(const Dependency &p_key, std::vector<uint32_t> &r_units);
};

} // namespace kas3
