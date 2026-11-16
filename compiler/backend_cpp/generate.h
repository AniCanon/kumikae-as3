// The C++ backend: a module of IR written as C++ source against the headers in runtime/.
#pragma once

#include "ir.h"

#include <string>
#include <vector>

namespace kas3 {
namespace backend_cpp {

struct File {
	std::string name; // "Player.cpp": no folder.
	std::string text;
};

// What generate() gives: the files, or the first operation it does not translate yet.
struct Generated {
	std::vector<File> files;
	std::string unsupported; // Empty when the whole program was translated.
};

// One .cpp for each class with code of its own, `program.h` (every declaration) and `program.cpp` (the
// functions of no class, and the table `kas3_program` returns).
Generated generate(const ir::Module &p_module);

} // namespace backend_cpp
} // namespace kas3
