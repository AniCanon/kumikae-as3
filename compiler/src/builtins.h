// The built-in declaration files, embedded at build time from compiler/builtins/*.as.
#pragma once

namespace kas3 {

struct BuiltinFile {
	const char *path;
	const char *text;
};

extern const BuiltinFile BUILTIN_FILES[];
extern const int BUILTIN_FILE_COUNT;

} // namespace kas3
