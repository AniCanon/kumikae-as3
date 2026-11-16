// What generated code calls out of line: the paths an error takes, a class's first use, the library the
// interpreter shares, and output. The host implements these (runtime/*.cpp); nothing here is a fast path.
// Each is declared in api.h, once, with every other function a program library is handed: here is what
// each is for.
#pragma once

#include "program.h"
#include "values.h"

namespace kas3 {
namespace rt {

// Errors. One that is raised sets c->failing; the function it is raised in then leaves by its error path.

// A call one deeper than MAX_DEPTH was asked for: AS3's Error #1023.
//     void stack_overflow(Context *c);
// A member of nothing was asked for: AS3's TypeError #1009.
//     void raise_null(Context *c);
// Memory ran out where the runtime itself asked for some: AS3's Error #1000.
//     void out_of_memory(Context *c);
// `throw`: any value, null too. It takes a reference of its own.
//     void throw_value(Context *c, Slot p_any);
// The error a `finally` caught (in the register numbered p_register) thrown again, with the trace it had.
// *r_traced says that the function's own line is in that trace already.
//     void rethrow(Context *c, Slot p_any, uint32_t p_register, bool *r_traced);
// Whether the error on its way out is of the type a catch takes: a class, an ANY_IS_ code, or NONE for all.
//     bool catches(Context *c, uint32_t p_type);
// The error is caught: it is no longer on its way out, and is the caller's, untyped. A finally's catch
// (p_rethrows) keeps the trace, with this function's line, for when the error is thrown again.
//     Slot caught(Context *c, uint32_t p_function, uint32_t p_line, uint32_t p_register, bool p_rethrows, bool *r_traced);
// The function (by its number) is left by an error: its line goes into the trace, unless it is in already.
//     void leave_failed(Context *c, uint32_t p_function, uint32_t p_line, bool p_traced);
// What an Error's getStackTrace() gives, for the error being made now: its name and message, then the
// functions in progress below its constructors. Only in a program that keeps frames (Program::keeps_stack).
//     Str *error_stack(Context *c, Object *p_error);
// A function with a finally returns: traces kept for it and never taken are forgotten.
//     void forget_kept(Context *c);

// A class is used for the first time by running code: its static fields, and its bases', are given their
// values.
//     void use_class(Context *c, uint32_t p_class);
//     bool is_class(const Context *c, const Object *p_object, uint32_t p_class);

// An operation carried out by the library the interpreter has (runtime/thunks.cpp): p_given are the values
// its ThunkInfo says are handed in, each a whole slot; they stay the caller's. What it hands back is the
// caller's to release. A register it moves on is written to *r_updated.
//     Slot thunk(Context *c, uint32_t p_thunk, const Slot *p_given, Slot *r_updated);

// One traced line: the texts with a space between, a missing one reading "null".
//     void trace(Context *c, Str *const *p_texts, uint32_t p_count);

} // namespace rt
} // namespace kas3
