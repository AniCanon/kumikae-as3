#pragma once

#include "ir_module.h"

namespace kas3::ir {

// Check table references before the interpreter indexes a decoded program.
bool binary_references_valid(const Module &p_module);

} // namespace kas3::ir
