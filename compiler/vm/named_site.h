// Resolution metadata for one by-name instruction. No entry holds a receiver or a script instance.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace kas3::vm {

struct NamedMember;
struct NamedOverflow;
struct Object;
struct NamedShape {
	uintptr_t type; // The host's class identity or engine value type.
	uint64_t script = 0; // An attached script's identity, zero for a bare receiver.
	Object *instance = nullptr;
	void *owner = nullptr; // The validated engine object, borrowed for this access.
	bool delegate = false; // Godot resolves the current non-AS3 script on this access.
};

enum class NamedResult : uint8_t { Declined, Done, Reused };

struct NamedCall {
	Object *receiver = nullptr;
	uint32_t function;
};

struct NamedSite {
	struct Entry {
		uintptr_t type = 0;
		uint64_t script = 0;
		std::optional<uint32_t> klass;
		std::optional<uint32_t> field;
		std::optional<uint32_t> method;
		const NamedMember *engine = nullptr;
	};
	std::array<Entry, 4> entries{};
	NamedOverflow *overflow = nullptr; // Owned by the current interpreter generation.
	uint64_t generation = 0;
	uint32_t used = 0;
	std::optional<uint32_t> delegated;
};

} // namespace kas3::vm
