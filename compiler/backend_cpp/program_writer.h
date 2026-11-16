// The parts of a generated program that are not function bodies: `program.h` (each class as a struct, every
// function declared) and the table `kas3_program` returns (runtime/program.h).
#pragma once

#include "ir.h"
#include "thunks.h"

#include <string>

namespace kas3 {
namespace backend_cpp {

// The line every generated file starts with.
std::string generated_notice(std::string_view p_from);
// Whether the class's objects are a struct of fields: not a vector, a table or an interface.
bool has_struct(const ir::Class &p_class);
std::string write_header(const ir::Module &p_module);
// The descriptors, in the program's namespace, and `kas3_program` after it.
std::string write_table(const ir::Module &p_module, const ThunkTable &p_thunks, bool p_keeps_stack);

} // namespace backend_cpp
} // namespace kas3
