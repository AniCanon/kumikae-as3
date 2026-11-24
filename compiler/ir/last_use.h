// Register ownership at a store shared by both execution modes.
#pragma once

#include "register_uses.h"

namespace kas3::ir {

// The register an instruction reads and also changes where it is: a loop's cursor, and one of Godot's
// values that Godot changes in place (a part of it set, a method called on it). NONE for the rest. The
// library works on a copy of what it is handed, so such a register is handed back.
uint32_t changed_in_place(const ir::Function &p_function, const ir::Instruction &p_in);

// Of the candidate stores, select those whose source value is dead on every successor.
std::vector<bool> last_stores(const Function &p_function, const std::vector<bool> &p_candidate);

// Insert releases on completion of the instruction that last uses an owned temporary.
void release_temporaries(Function &r_function, bool p_stripped = false);

} // namespace kas3::ir
