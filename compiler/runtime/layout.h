// The layout a program library and the runtime that loads it share. A program library is C++ built against
// these headers at one time and loaded by a host built at another: what both read by its place in memory
// (the program's table and what it lists, the context, the tables of the runtime's and the engine's
// functions, an object's and a vector's header) must be the same in both, or the host misreads the library.
// The library says which layout it was built for, first thing in its table (Program::layout), and a host
// reads nothing more of a table that says another.
#pragma once

#include "api.h"
#include "engine.h"
#include "program.h"
#include "values.h"

namespace kas3 {
namespace rt {

// "KAS3", then the layout's number. Add one whenever a program library built before the change would be
// misread after it: a member added to, taken from, moved in or retyped in one of the structs below, an entry
// added to or moved in KAS3_RUNTIME_API or EngineApi, a number generated code is written with given another
// meaning (an ir::Kind, an ir::Bare, a thunk's shape), or an inline function of these headers and the host
// coming to disagree about what a value's bits mean (vm/any.h). A change that only adds an entry at the end
// of a table counts too: an older library is handed a table it reads the same, but a newer one would call
// what an older host does not have.
inline constexpr uint64_t RUNTIME_LAYOUT = 0x4b4153330000000cull;

// A size that moves without the number above moving does not build: set the number, then the size. (A
// change that keeps every size is not caught here: the list above is what decides.)
static_assert(sizeof(void *) != 8 ||
				(sizeof(Program) == 248 && sizeof(ClassInfo) == 144 && sizeof(FieldInfo) == 24 && sizeof(FunctionInfo) == 72 &&
						sizeof(GlobalInfo) == 24 && sizeof(ThunkInfo) == 72 && sizeof(NamedMethod) == 16 && sizeof(Text) == 16 &&
						sizeof(KnownClasses) == 60 && sizeof(Context) == 256 && sizeof(Api) == 728 && sizeof(EngineApi) == 136 &&
						sizeof(HandleHead) == 32 && sizeof(Resolved) == 64 && sizeof(Object) == 32 && sizeof(Vector) == 64 && sizeof(Str) == 8 && sizeof(Slot) == 8 &&
						sizeof(StackFrame) == 16 && sizeof(ObjectCache) == 400),
		"what a program library and its host share has changed size: add one to RUNTIME_LAYOUT, then set the size here");

// Whether a program's table was built for this layout: asked before anything else of the table is read.
inline bool same_layout(const Program &p_program) {
	return p_program.layout == RUNTIME_LAYOUT;
}
// What is said of a program library whose table was not.
inline constexpr const char *OTHER_LAYOUT = "it was built for another version of the runtime: build the program library again";

} // namespace rt
} // namespace kas3
