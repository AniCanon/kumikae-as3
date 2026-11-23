// Resolution metadata for one by-name instruction. No entry holds a receiver or a script instance.
#pragma once

#include <array>
#include <cstdint>

namespace kas3::vm {

struct NamedMember;
struct Object;
struct NamedShape {
	uintptr_t type = 0; // The host's class identity, or an engine value type. Zero declines.
	uint64_t script = 0; // An attached script's identity, zero for a bare receiver.
	Object *instance = nullptr;
	void *owner = nullptr; // The validated engine object, borrowed for this access.
};

struct NamedSite {
	struct Entry {
		uintptr_t type = 0;
		uint64_t script = 0;
		uint32_t klass = UINT32_MAX;
		uint32_t field = UINT32_MAX;
		const NamedMember *engine = nullptr;
	};
	std::array<Entry, 4> entries{};
	uint64_t generation = 0;
	uint32_t used = 0;
};

} // namespace kas3::vm
