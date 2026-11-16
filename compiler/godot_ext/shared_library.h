// A shared library loaded by its path, on whichever system: the project's program library is one.
#pragma once

#include <string>

namespace kas3 {

class SharedLibrary {
	void *handle_ = nullptr;

public:
	// The file's name for a library called `name` here: "libname.dylib", "libname.so", "name.dll".
	static std::string file_name(const std::string &p_name);
	SharedLibrary() = default;
	~SharedLibrary() { close(); }
	SharedLibrary(const SharedLibrary &) = delete;
	SharedLibrary &operator=(const SharedLibrary &) = delete;
	// False, with the system's words for why, if it can't be loaded. Its symbols are its own: none of them
	// is found by another library's lookups.
	[[nodiscard]] bool open(const std::string &p_path, std::string *r_why);
	// The address of a symbol it exports, or null.
	[[nodiscard]] void *symbol(const char *p_name) const;
	void close();
};

} // namespace kas3
