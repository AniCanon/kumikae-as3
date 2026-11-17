// The memory of a handle: a godot::Variant the interpreter holds (vm/host.h, EngineHandles). Every handle is
// made and freed here, so that a freed one's memory is kept for the next: a value Godot gives back costs no
// allocation once the game runs. Off the main thread (Godot freeing an object there) memory is allocated
// and freed as usual; both ways take it from the same allocator, so either may free the other's.
//
// Before each handle is its head (rt::HandleHead, runtime/engine.h), which generated code reads too: how
// many hold a handle that is shared and not copied (one on an object that is not reference-counted:
// GodotHost::copy), and the object the host last found behind it (native_program.cpp).
#pragma once

#include "engine.h"

#include <godot_cpp/variant/variant.hpp>

#include <new>
#include <utility>

namespace kas3 {

// Memory for one handle, its head before it and set for a handle one holds, that nothing is known of yet.
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
