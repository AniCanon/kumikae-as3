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

// Whether the program may read what an Error's getStackTrace() gives: the stack where the error was made,
// which generated code keeps only for a program that does (runtime/values.h, StackFrame).
bool reads_error_stack(const ir::Module &p_module);

} // namespace backend_cpp
} // namespace kas3
