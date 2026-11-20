#include "shared_library.h"

#if defined(_WIN32)
// The Windows half is written from the documentation and has not been built or run.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace kas3 {

std::string SharedLibrary::file_name(const std::string &p_name) {
#if defined(_WIN32)
	return p_name + ".dll";
#elif defined(__APPLE__)
	return "lib" + p_name + ".dylib";
#elif defined(__EMSCRIPTEN__)
	return "lib" + p_name + ".wasm";
#else
	return "lib" + p_name + ".so";
#endif
}

#if defined(_WIN32)

bool SharedLibrary::open(const std::string &p_path, std::string *r_why) {
	close();
	// The path is UTF-8, as everything of Godot's is; the system takes UTF-16.
	const int length = MultiByteToWideChar(CP_UTF8, 0, p_path.c_str(), -1, nullptr, 0);
	std::wstring wide(length > 0 ? static_cast<size_t>(length) : 1, L'\0');
	if (length > 0) {
		MultiByteToWideChar(CP_UTF8, 0, p_path.c_str(), -1, wide.data(), length);
	}
	handle_ = LoadLibraryExW(wide.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (!handle_) {
		*r_why = "the system could not load it (error " + std::to_string(GetLastError()) + ")";
	}
	return handle_ != nullptr;
}

void *SharedLibrary::symbol(const char *p_name) const {
	return handle_ ? reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle_), p_name)) : nullptr;
}

void SharedLibrary::close() {
	if (handle_) {
		FreeLibrary(static_cast<HMODULE>(handle_));
		handle_ = nullptr;
	}
}

#else

bool SharedLibrary::open(const std::string &p_path, std::string *r_why) {
	close();
	handle_ = dlopen(p_path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!handle_) {
		const char *said = dlerror();
		*r_why = said ? said : "the system could not load it";
	}
	return handle_ != nullptr;
}

void *SharedLibrary::symbol(const char *p_name) const {
	return handle_ ? dlsym(handle_, p_name) : nullptr;
}

void SharedLibrary::close() {
	if (handle_) {
		dlclose(handle_);
		handle_ = nullptr;
	}
}

#endif

} // namespace kas3
