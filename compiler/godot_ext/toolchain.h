// Which C++ compiler builds a project's program library, found without running anything: a warm start of
// a game asks every time, and must not pay for a program started (tests/test_play_cache.cpp).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace kas3 {
namespace toolchain {

struct Compiler {
	std::string path; // What is run.
	// Which compiler that is: the file that is the compiler, its size and when it was written. Another
	// version of the compiler is another identity, and what it built is not taken for this one's.
	std::string identity;
};

struct Search {
	std::string named; // The compiler a person named (KAS3_CXX): a path, or a name on the PATH. Empty for none.
	std::string path; // The PATH.
	// Where the system keeps programs that only stand for the developer tools' own (macOS: /usr/bin, whose
	// `c++` offers to install the tools when they are not there, in a window). Empty where there are none.
	std::string shims;
	// The folders the developer tools may be in, the first that is there being the one in use (macOS:
	// DEVELOPER_DIR, what `xcode-select` chose, Xcode, the command line tools).
	std::vector<std::string> developer;
};

inline bool runs(const std::filesystem::path &p_file) {
	std::error_code failed;
	const std::filesystem::file_status status = std::filesystem::status(p_file, failed);
	using std::filesystem::perms;
	return !failed && std::filesystem::is_regular_file(status) &&
			(status.permissions() & (perms::owner_exec | perms::group_exec | perms::others_exec)) != perms::none;
}

inline std::optional<std::string> on_path(std::string_view p_name, std::string_view p_path) {
	while (!p_path.empty()) {
		const size_t colon = p_path.find(':');
		const std::string_view folder = p_path.substr(0, colon);
		p_path = colon == std::string_view::npos ? std::string_view() : p_path.substr(colon + 1);
		const std::filesystem::path file = std::filesystem::path(folder) / p_name;
		if (!folder.empty() && runs(file)) {
			return file.string();
		}
	}
	return std::nullopt;
}

inline std::string identity_of(const std::filesystem::path &p_file) {
	std::error_code failed;
	const uintmax_t size = std::filesystem::file_size(p_file, failed);
	const auto written = std::filesystem::last_write_time(p_file, failed).time_since_epoch().count();
	return p_file.string() + "|" + std::to_string(size) + "|" + std::to_string(static_cast<long long>(written));
}

// The developer tools' own compiler, in the first of the folders that is there; none if none has one.
inline std::optional<std::filesystem::path> tools_compiler(const std::vector<std::string> &p_developer) {
	for (const std::string &folder : p_developer) {
		std::error_code failed;
		if (folder.empty() || !std::filesystem::is_directory(folder, failed)) {
			continue;
		}
		for (const char *place : { "Toolchains/XcodeDefault.xctoolchain/usr/bin/clang", "usr/bin/clang" }) {
			if (runs(std::filesystem::path(folder) / place)) {
				return std::filesystem::path(folder) / place;
			}
		}
		return std::nullopt; // The folder in use has no compiler: a later one is not the one that would run.
	}
	return std::nullopt;
}

// The folders the developer tools may be in on macOS, the first that is there being the one in use:
// DEVELOPER_DIR, what `xcode-select` chose, Xcode, and the command line tools.
inline std::vector<std::string> apple_developer(const std::string &p_developer_dir) {
	std::error_code failed;
	return { p_developer_dir, std::filesystem::read_symlink("/var/db/xcode_select_link", failed).string(),
		"/Applications/Xcode.app/Contents/Developer", "/Library/Developer/CommandLineTools" };
}

// The compiler: the one named, or `c++`, `clang++` or `g++`, in that order, each wherever the PATH has it.
// None if it is not there, or if it only stands for developer tools that are not installed.
inline std::optional<Compiler> find_compiler(const Search &p_search) {
	std::optional<std::string> found;
	if (p_search.named.find('/') != std::string::npos) {
		found = runs(p_search.named) ? std::optional<std::string>(p_search.named) : std::nullopt;
	} else if (!p_search.named.empty()) {
		found = on_path(p_search.named, p_search.path);
	} else {
		for (const char *name : { "c++", "clang++", "g++" }) {
			if ((found = on_path(name, p_search.path))) {
				break;
			}
		}
	}
	if (!found) {
		return std::nullopt;
	}
	const std::filesystem::path file(*found);
	if (!p_search.shims.empty() && file.parent_path() == std::filesystem::path(p_search.shims)) {
		const std::optional<std::filesystem::path> real = tools_compiler(p_search.developer);
		return real ? std::optional<Compiler>(Compiler{ *found, identity_of(*real) }) : std::nullopt;
	}
	return Compiler{ *found, identity_of(file) };
}

} // namespace toolchain
} // namespace kas3
