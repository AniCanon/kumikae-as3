// The memory of a handle: a godot::Variant the interpreter holds (vm/host.h, EngineHandles). Every handle is
// made and freed here, so that a freed one's memory is kept for the next: a value Godot gives back costs no
// allocation once the game runs. Off the main thread (Godot freeing an object there) memory is allocated
// and freed as usual; both ways take it from the same allocator, so either may free the other's.
#pragma once

#include <godot_cpp/variant/variant.hpp>

#include <new>
#include <utility>

namespace kas3 {

void *handle_memory();
void free_handle_memory(void *p_memory);

template <class... T>
godot::Variant *make_handle(T &&...p_value) {
	return new (handle_memory()) godot::Variant(std::forward<T>(p_value)...);
}

inline void free_handle(godot::Variant *p_handle) {
	p_handle->~Variant();
	free_handle_memory(p_handle);
}

} // namespace kas3
