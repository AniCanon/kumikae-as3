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
	// As one unit: the files of classes that the other way of writing the same program makes, and this one
	// does not. Left in the folder from an earlier run, they would be the program's code a second time.
	std::vector<std::string> not_written;
	std::string unsupported; // Empty when the whole program was translated.
};

struct Options {
	// The whole program in `program.cpp`, where the C++ compiler sees every function at once and can put
	// one inside another (a constructor inside what makes the object): what a release is built from. The
	// other way, a file for each class, is what an editor rebuilds least of after an edit.
	bool one_unit = false;
};

// One .cpp for each class with code of its own, `program.h` (every declaration) and `program.cpp` (the
// functions of no class, and the table `kas3_program` returns).
Generated generate(const ir::Module &p_module, const Options &p_options = Options());

} // namespace backend_cpp
} // namespace kas3
