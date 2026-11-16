// The library's functions of plain values (vm/library.h), as generated code calls them: with no thunk and
// no interpreter between. Each is known by the name ir/library_entries.h gives the instruction it carries
// out, and takes what that instruction reads: its number (the second operand), then its values in the
// instruction's order, each a whole slot that stays the caller's. What it gives is written to *r_out with
// a reference that is the caller's, or let go of where r_out is null. An error it raises is on its way out
// when it returns (c->failing), and nothing was written.
#pragma once

#include "values.h"

namespace kas3 {
namespace rt {

// StrCall for split() and the URI functions.
void text_call(Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out);

} // namespace rt
} // namespace kas3
