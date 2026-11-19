// Where Play keeps the program libraries it builds, apart from Godot: which build a library is of (its
// key), what its files are called, which of them are taken away, and the command that builds one
// (godot_ext/play_build.cpp does it; tests/test_play_cache.cpp holds these rules with folders of its own).
//
// One folder holds every build, each under its key:
//   <key>/                    the C++ the backend wrote, build.log, and in kas3/ the runtime's headers it is built against
//   libkas3_program_<key>...  the library, under that name only once it is whole
//   <key>.building            a build of it is going on: who does it (written as it starts, taken away as it ends)
//   <key>.failed              it could not be built; why, in one line
// A library is never written over: another build has another key, and so another name.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kas3 {
namespace play {

// What generated code is built with (the native backend's design): as the tests and the benchmarks build
// it, without their -Werror, since a warning in generated code is not the person's to answer.
inline constexpr const char *COMPILE_FLAGS = "-std=c++17 -O2 -fno-exceptions -fno-rtti -ffp-contract=off -fno-unroll-loops -fvisibility=hidden";
inline std::string library_flags() {
#if defined(__APPLE__)
	return std::string(COMPILE_FLAGS) + " -shared";
#else
	return std::string(COMPILE_FLAGS) + " -shared -fPIC -Wl,-z,defs -fno-math-errno";
#endif
}
// How many builds are kept besides those in use: a game and its tests are two programs, and a person
// goes back and forth between two versions of a script.
inline constexpr size_t KEPT_BUILDS = 3;
// How long another run's build is taken to be still going on, by its marker's age.
inline constexpr int64_t BUILDING_SECONDS = 600;
inline constexpr size_t KEY_LENGTH = 16;
// The folder of a build's own that holds the runtime's headers.
inline constexpr const char *HEADERS = "kas3";

// Everything that makes a program library what it is. Two builds with the same parts are the same library.
struct KeyParts {
	uint64_t fingerprint = 0; // The scripts, as the module compiled from them (ir::fingerprint).
	uint64_t layout = 0; // rt::RUNTIME_LAYOUT.
	std::string_view identity; // The backend and the runtime's headers this extension carries.
	std::string_view compiler; // Which C++ compiler (toolchain::Compiler::identity).
	std::string_view flags;
};

inline uint64_t mixed(uint64_t p_hash, std::string_view p_text) {
	for (const char c : p_text) {
		p_hash = (p_hash ^ static_cast<unsigned char>(c)) * 0x100000001b3ull; // FNV-1a.
	}
	// Each part ends, so that two parts are not one text cut elsewhere.
	return (p_hash ^ 0xff) * 0x100000001b3ull;
}

inline std::string build_key(const KeyParts &p_parts) {
	uint64_t hash = 0xcbf29ce484222325ull;
	hash = mixed(hash, std::to_string(p_parts.fingerprint));
	hash = mixed(hash, std::to_string(p_parts.layout));
	hash = mixed(hash, p_parts.identity);
	hash = mixed(hash, p_parts.compiler);
	hash = mixed(hash, p_parts.flags);
	char text[KEY_LENGTH + 1];
	std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
	return text;
}

// The library's name without what the system adds to it ("lib", ".dylib").
inline std::string library_stem(std::string_view p_key) {
	return "kas3_program_" + std::string(p_key);
}
inline std::string failed_name(std::string_view p_key) {
	return std::string(p_key) + ".failed";
}
inline std::string building_name(std::string_view p_key) {
	return std::string(p_key) + ".building";
}

inline bool is_key(std::string_view p_text) {
	return p_text.size() == KEY_LENGTH &&
			std::all_of(p_text.begin(), p_text.end(), [](char p_c) { return (p_c >= '0' && p_c <= '9') || (p_c >= 'a' && p_c <= 'f'); });
}

// The build a file or a folder of the cache belongs to, by its name; none for anything else.
inline std::optional<std::string> key_of(std::string_view p_name) {
	constexpr std::string_view stem = "kas3_program_";
	if (const size_t at = p_name.find(stem); at != std::string_view::npos) {
		const std::string_view rest = p_name.substr(at + stem.size());
		const bool ends = rest.size() == KEY_LENGTH || (rest.size() > KEY_LENGTH && rest[KEY_LENGTH] == '.');
		return ends && is_key(rest.substr(0, KEY_LENGTH)) ? std::optional<std::string>(rest.substr(0, KEY_LENGTH)) : std::nullopt;
	}
	const std::string_view first = p_name.substr(0, p_name.find('.'));
	const std::string_view rest = p_name.substr(first.size());
	return is_key(first) && (rest.empty() || rest == ".failed" || rest == ".building") ? std::optional<std::string>(first) : std::nullopt;
}

// A file or a folder of the cache: its name, and when it was last written, in seconds.
struct Entry {
	std::string name;
	int64_t written = 0;
};

// The names to take away: everything of every build but those in use (p_keep) and the p_newest newest of
// the others. A build is as new as the newest of its files, so one used again is kept by touching one.
inline std::vector<std::string> stale(const std::vector<Entry> &p_entries, const std::vector<std::string> &p_keep, size_t p_newest) {
	std::vector<std::pair<int64_t, std::string>> builds; // When, and the key: each key once.
	for (const Entry &entry : p_entries) {
		const std::optional<std::string> key = key_of(entry.name);
		if (!key || std::find(p_keep.begin(), p_keep.end(), *key) != p_keep.end()) {
			continue;
		}
		const auto found = std::find_if(builds.begin(), builds.end(), [&](const auto &p_build) { return p_build.second == *key; });
		if (found == builds.end()) {
			builds.emplace_back(entry.written, *key);
		} else {
			found->first = std::max(found->first, entry.written);
		}
	}
	std::sort(builds.begin(), builds.end(), [](const auto &p_a, const auto &p_b) { return p_a > p_b; });
	std::vector<std::string> gone;
	for (const Entry &entry : p_entries) {
		const std::optional<std::string> key = key_of(entry.name);
		const auto found = key ? std::find_if(builds.begin(), builds.end(), [&](const auto &p_build) { return p_build.second == *key; }) : builds.end();
		if (found != builds.end() && static_cast<size_t>(found - builds.begin()) >= p_newest) {
			gone.push_back(entry.name);
		}
	}
	return gone;
}

// One word to the shell, whatever is in it.
inline std::string shell_word(std::string_view p_text) {
	std::string out = "'";
	for (const char c : p_text) {
		if (c == '\'') {
			out += "'\\''";
		} else {
			out += c;
		}
	}
	return out + "'";
}

struct Build {
	std::string folder; // The cache.
	std::string key;
	std::string library; // The library's file name there.
	std::string compiler;
	std::string flags;
	std::string nice; // The program that runs another at a lower priority, or empty.
};

// What /bin/sh is given to build a library, from the cache's folder: the compiler writes a file of the
// build's own, which takes the library's name only when it is whole (a run that starts meanwhile never
// loads half a library, and two builds of one key do not write one file); what the compiler said is in the
// build's log; a failure leaves the marker; the marker of a build going on names the shell that does it,
// and is taken away either way.
inline std::string build_script(const Build &p_build) {
	const std::string library = shell_word(p_build.library);
	const std::string made = library + ".tmp.$$";
	// Nothing of the game's is kept open: whoever reads what the game says (an IDE's console, a test) is
	// not kept waiting for the build once the game has ended.
	std::string script = "exec < /dev/null > /dev/null 2>&1\n";
	script += "cd " + shell_word(p_build.folder) + " || exit 1\n";
	script += "echo $$ > " + shell_word(building_name(p_build.key)) + "\n"; // Who builds: the marker counts while that is alive.
	script += p_build.nice.empty() ? std::string() : shell_word(p_build.nice) + " -n 10 ";
	script += shell_word(p_build.compiler) + " " + p_build.flags + " -I " + shell_word(p_build.key + "/" + HEADERS) + " " + shell_word(p_build.key + "/program.cpp") + " -o " +
			made + " > " + shell_word(p_build.key + "/build.log") + " 2>&1 && /bin/mv -f " + made + " " + library + "\n";
	script += "status=$?\n";
	script += "if [ $status -ne 0 ]; then /bin/rm -f " + made + "; echo 'the C++ compiler failed' > " + shell_word(failed_name(p_build.key)) + "; fi\n";
	script += "/bin/rm -f " + shell_word(building_name(p_build.key)) + "\n";
	script += "exit $status\n";
	return script;
}

// What the project's setting is where the project does not set it. On, where Play's build has been built
// and run (macOS): native code stops a call that runs too long as the interpreter does, and an edit made
// while a game runs as native code moves the game onto the interpreter with every object updated in place
// as the interpreter's own are. Off on a system where the build is written and has never been run.
#if defined(__APPLE__)
inline constexpr bool BY_DEFAULT = true;
#else
inline constexpr bool BY_DEFAULT = false;
#endif

// Whether Play builds and runs native code: what the environment says (KAS3_NATIVE_PLAY: 0, off or false
// against; 1, on or true for), and the project's setting where it says neither.
inline bool wanted(const char *p_environment, bool p_setting) {
	const std::string_view said = p_environment ? p_environment : "";
	if (said == "0" || said == "off" || said == "false") {
		return false;
	}
	if (said == "1" || said == "on" || said == "true") {
		return true;
	}
	return p_setting;
}

// Whether a build whose marker was written then can still be going on now, if whoever the marker names is
// still alive: a build that was killed leaves its marker, and is not waited for.
inline bool still_building(int64_t p_written, int64_t p_now, bool p_alive = true) {
	return p_alive && p_now - p_written < BUILDING_SECONDS;
}

} // namespace play
} // namespace kas3
