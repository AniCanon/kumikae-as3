// The C++ stack a program runs on. The interpreter keeps a program's frames on the heap, but its library
// calls the program back in C++ (a sort's comparison, a toString()), and native code's frames are the
// thread's: both stop at one condition, the thread's stack nearly used up, which is AS3's Error #1023
// like calls nested too deep and never a crash.
#pragma once

#include <cstddef>
#include <cstdint>

namespace kas3 {
namespace vm {

// What a program run on its own is given (address space, not memory: a page is used when touched): enough
// for 10,000 calls of any likely size, so that the count of calls is what ends a recursion.
constexpr size_t PROGRAM_STACK_BYTES = size_t(512) << 20;
// Kept free above the stack's end: what the library does between two looks at the floor, and an error's
// way out.
constexpr size_t STACK_MARGIN = size_t(1) << 20;

// The address the calling thread's stack is nearly used up at: its end and the margin (half of a stack
// too small for one). Where the system does not say where the stack ends, as far below the caller as the
// least of the systems' default stacks leaves room for.
uintptr_t stack_floor();

// Whether the function this is written in has its frame below the floor. Always inline: it is the frame of
// the function that asks.
__attribute__((always_inline)) inline bool stack_below(uintptr_t p_floor) {
	return reinterpret_cast<uintptr_t>(__builtin_frame_address(0)) < p_floor;
}

// Runs the function on a thread of its own with a stack of that size, and waits for it. False, with
// nothing run, where no such thread could be made.
[[nodiscard]] bool run_with_stack(size_t p_bytes, void (*p_run)(void *), void *p_data);
// Runs it with the stack a program is given, or the one KAS3_STACK_KB asks for in kilobytes (for the
// tests: a stack small enough to run out of); on the calling thread where no other could be made.
void run_as_program(void (*p_run)(void *), void *p_data);

} // namespace vm
} // namespace kas3
