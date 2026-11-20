// VM contracts that depend on wasm32's pointer width, heap alignment and data stack.
#include "any.h"
#include "check.h"
#include "stack.h"

#include <emscripten/stack.h>

#include <algorithm>

using namespace kas3::vm;

static void test_boxed_pointers() {
	// Bit 3 is part of a valid wasm32 address. None of the boxed pointers is dereferenced.
	alignas(16) char storage[16];
	void *address = storage + 8;
	for (const Slot value : { any::string(static_cast<Str *>(address)), any::object(static_cast<Object *>(address)),
				 any::wide(static_cast<Wide *>(address)), any::godot_object(address), any::godot_value(address) }) {
		CHECK(any::pointer(value) == address);
	}
}

static void test_vector_storage() {
	for (const bool narrow : { false, true }) {
		Vector vector{};
		vector.narrow = narrow;
		const uint32_t first_too_large = SIZE_MAX / place_size(&vector) + 1;
		// Refusal must happen before touching the allocator. The injected failure is still pending.
		fail_allocations(1);
		CHECK(!vector_reserve(&vector, first_too_large));
		CHECK(allocation_fails());
		CHECK(vector.places == nullptr && vector.capacity == 0);
	}
}

int main() {
	test_vector_storage();
	const uintptr_t end = emscripten_stack_get_end();
	const size_t size = emscripten_stack_get_base() - end;
	CHECK_EQ(stack_floor(), end + std::min(STACK_MARGIN, size / 2));
	test_boxed_pointers();
	return check::finish();
}
