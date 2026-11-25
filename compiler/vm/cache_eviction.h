// Replacing one pseudo-random entry avoids flushing a hot working set on a capacity miss.
#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace kas3::vm {

class CacheEviction {
	uint32_t state_ = 1;

public:
	static constexpr size_t CAPACITY = 64;

	template <class Map>
	void make_room(Map &p_entries) {
		if (p_entries.size() >= CAPACITY) {
			state_ = state_ * 1664525u + 1013904223u;
			p_entries.erase(std::next(p_entries.begin(), (state_ >> 16) % CAPACITY));
		}
	}
};

} // namespace kas3::vm
