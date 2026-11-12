// The built-in declaration files, embedded at build time from compiler/builtins/*.as.
#pragma once

#include <string>

namespace kas3 {

struct BuiltinFile {
	const char *path;
	const char *text;
};

extern const BuiltinFile BUILTIN_FILES[];
extern const int BUILTIN_FILE_COUNT;

// Godot's API as AS3 declarations for the `godot` package (compiler/godot/godot.as). It is not loaded with
// the built-ins: add it to a Compilation as a declaration file under GODOT_DECLARATIONS_PATH.
std::string godot_declarations();
inline const char *const GODOT_DECLARATIONS_PATH = "<godot>/godot.as";

} // namespace kas3
