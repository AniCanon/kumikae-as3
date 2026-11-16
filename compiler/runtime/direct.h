// The library's functions of plain values (vm/library.h), as generated code calls them: with no thunk and
// no interpreter between. Each is known by the name ir/library_entries.h gives the instruction it carries
// out, and takes what that instruction reads: the entry's number, then its values in the instruction's
// order, each a whole slot that stays the caller's. What it gives is written to *r_out with
// a reference that is the caller's, or let go of where r_out is null. An error it raises is on its way out
// when it returns (c->failing), and nothing was written.
//
// Declared with the rest of what generated code calls out of line (api.h): a program library reaches them
// through the host's table.
#pragma once

#include "values.h"

namespace kas3 {
namespace rt {

// StrCall for split() and the URI functions.
//     void text_call(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
// A number or a Boolean as a String: p_which is the kind (an ir::Kind) of the one value.
//     void text_of_value(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
// Concat: one String after another, a missing one reading "null".
//     void text_joined(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
// NumText: p_which is the format (an ir::NumberFormat), with ir::NUMBER_TEXT_WIDE for an Int64.
//     void number_text(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
// A typed value (p_which its kind) as an untyped one; an untyped value stored into a place (p_which the
// class or engine name checked, or NONE; after the value, the place's kind); `is` (p_which the type).
//     void value_to_any(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
//     void any_to_value(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
//     void any_is_type(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
// New: an object of the class p_which, as it is before its constructor runs. TableNew: an Object or a
// Dictionary of the class p_which (its one value: whether its keys are held weakly, or nothing).
//     void object_new(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
//     void table_new(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
// VecCall: p_which is the method (an ir::VectorMethod); the vector, then what the method takes.
//     void vector_call(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
// A vector's element read (the vector, the index) and written (the vector, the index, the value): p_which
// is the kind (an ir::Kind) the index is held as. Its length and whether that is fixed, read and set.
//     void vector_get(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
//     void vector_set(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
//     void vector_length(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
//     void vector_set_length(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
//     void vector_fixed(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
//     void vector_set_fixed(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
// A new vector (its length, whether that is fixed), a new Array (what `new Array` was given) and
// Vector.<T>(x): p_which is the class.
//     void vector_new(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
//     void array_new(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);
//     void vector_from(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);

} // namespace rt
} // namespace kas3
