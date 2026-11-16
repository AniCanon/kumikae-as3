// Which programs the backend translates: asked before anything is written.
#pragma once

#include "ir.h"

#include <optional>
#include <string>

namespace kas3 {
namespace backend_cpp {

// The first thing in the module that the backend does not translate yet, in words for
// "not yet supported: ...", or nothing if it translates all of it.
std::optional<std::string> first_unsupported(const ir::Module &p_module);

} // namespace backend_cpp
} // namespace kas3
