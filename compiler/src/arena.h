// A bump allocator for syntax-tree nodes. Everything it hands out lives until the arena is destroyed, and
// nothing is destructed individually, so only trivially destructible types may be allocated.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace kas3 {

class Arena {
	static const size_t CHUNK = 64 * 1024;

	std::vector<char *> chunks_;
	char *cursor_ = nullptr;
	char *end_ = nullptr;

	void *allocate(size_t p_size, size_t p_align) {
		uintptr_t aligned = ((uintptr_t)cursor_ + (p_align - 1)) & ~(uintptr_t)(p_align - 1);
		if (cursor_ == nullptr || aligned + p_size > (uintptr_t)end_) {
			// Sizes above the chunk size get a chunk of their own.
			size_t bytes = p_size + p_align > CHUNK ? p_size + p_align : CHUNK;
			char *chunk = static_cast<char *>(std::malloc(bytes));
			chunks_.push_back(chunk);
			cursor_ = chunk;
			end_ = chunk + bytes;
			aligned = ((uintptr_t)cursor_ + (p_align - 1)) & ~(uintptr_t)(p_align - 1);
		}
		cursor_ = (char *)(aligned + p_size);
		return (void *)aligned;
	}

public:
	Arena() {}
	Arena(const Arena &) = delete;
	Arena &operator=(const Arena &) = delete;
	~Arena() {
		for (char *chunk : chunks_) {
			std::free(chunk);
		}
	}

	template <class T, class... Args>
	T *make(Args &&...p_args) {
		static_assert(std::is_trivially_destructible<T>::value, "arena objects are never destructed");
		return new (allocate(sizeof(T), alignof(T))) T(std::forward<Args>(p_args)...);
	}

	// Zero-initialised. Never null, even for a count of zero.
	template <class T>
	T *make_array(size_t p_count) {
		static_assert(std::is_trivially_destructible<T>::value, "arena objects are never destructed");
		size_t bytes = sizeof(T) * (p_count ? p_count : 1);
		void *memory = allocate(bytes, alignof(T));
		std::memset(memory, 0, bytes);
		return static_cast<T *>(memory);
	}

	std::string_view copy(std::string_view p_text) {
		char *memory = static_cast<char *>(allocate(p_text.size() ? p_text.size() : 1, 1));
		if (!p_text.empty()) {
			std::memcpy(memory, p_text.data(), p_text.size());
		}
		return std::string_view(memory, p_text.size());
	}
};

} // namespace kas3
