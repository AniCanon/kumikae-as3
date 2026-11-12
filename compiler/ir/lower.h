// From a checked program to the IR.
#pragma once

#include "ir.h"
#include "names.h"

namespace kas3 {
namespace ir {

// Lowers every file of the compilation that is not a declaration file. The compilation must have been
// checked and have no errors. A construct the interpreter does not run yet is reported on its file as
// "... is not supported by the interpreter yet", and the result is then false.
// The program starts by constructing the first class of the first file's package block.
bool lower(Compilation &p_compilation, Module *r_module);

} // namespace ir
} // namespace kas3
