// What Play builds the project's program library by (godot_ext/play_cache.h, godot_ext/toolchain.h), with
// folders of the test's own and no Godot: which build a library is of, what its files are called, which
// are taken away, the command that builds one, the switch, and which C++ compiler is found.
#include "check.h"
#include "scratch.h"

#include "../godot_ext/play_cache.h"
#include "../godot_ext/toolchain.h"

#include <algorithm>
#include <fstream>

using namespace kas3;

namespace {

play::KeyParts parts() {
	return play::KeyParts{ 0x1234, 5, "runtime-a", "/usr/bin/clang|10|20", "-O2" };
}

bool hex16(const std::string &p_key) {
	return p_key.size() == 16 && std::all_of(p_key.begin(), p_key.end(), [](char p_c) { return (p_c >= '0' && p_c <= '9') || (p_c >= 'a' && p_c <= 'f'); });
}

std::filesystem::path program(const std::filesystem::path &p_folder, const char *p_name) {
	std::filesystem::create_directories(p_folder);
	const std::filesystem::path file = p_folder / p_name;
	std::ofstream(file) << "#!/bin/sh\n";
	std::filesystem::permissions(file, std::filesystem::perms::owner_all);
	return file;
}

} // namespace

// The key is the same for the same build, and another for anything that makes the library another: the
// scripts, the runtime's layout, the runtime and backend the extension carries, the compiler, the flags.
static void test_key() {
	const std::string key = play::build_key(parts());
	CHECK(hex16(key));
	CHECK_EQ(play::build_key(parts()), key);
	play::KeyParts other = parts();
	other.fingerprint++;
	CHECK(play::build_key(other) != key);
	other = parts();
	other.layout++;
	CHECK(play::build_key(other) != key);
	other = parts();
	other.identity = "runtime-b";
	CHECK(play::build_key(other) != key);
	other = parts();
	other.compiler = "/usr/bin/clang|10|21";
	CHECK(play::build_key(other) != key);
	other = parts();
	other.flags = "-O1";
	CHECK(play::build_key(other) != key);
	// What one part ends with is not what the next begins with.
	other = parts();
	other.identity = "runtime-a/";
	other.compiler = "usr/bin/clang|10|20";
	CHECK(play::build_key(other) != key);
}

// Every file of a build carries its key, and nothing else is taken for one.
static void test_names() {
	const std::string key = "0123456789abcdef";
	CHECK_EQ(play::library_stem(key), std::string("kas3_program_0123456789abcdef"));
	CHECK_EQ(play::key_of("libkas3_program_0123456789abcdef.dylib").value_or(""), key);
	CHECK_EQ(play::key_of("kas3_program_0123456789abcdef.dll").value_or(""), key);
	CHECK_EQ(play::key_of("libkas3_program_0123456789abcdef.dylib.tmp.4411").value_or(""), key);
	CHECK_EQ(play::key_of(key).value_or(""), key);
	CHECK_EQ(play::key_of(play::failed_name(key)).value_or(""), key);
	CHECK_EQ(play::key_of(play::building_name(key)).value_or(""), key);
	CHECK(!play::key_of("libkas3_program.dylib"));
	CHECK(!play::key_of("libkas3_program_0123456789abcdeg.dylib"));
	CHECK(!play::key_of("0123456789abcde"));
	CHECK(!play::key_of("0123456789ABCDEF"));
	CHECK(!play::key_of("notes.txt"));
	CHECK(!play::key_of(""));
}

// The builds kept are those in use and the newest few besides; everything of any other build goes, and a
// file that is no build's is never touched.
static void test_stale() {
	const std::vector<play::Entry> entries = {
		{ "aaaaaaaaaaaaaaaa", 10 }, { "libkas3_program_aaaaaaaaaaaaaaaa.dylib", 11 },
		{ "bbbbbbbbbbbbbbbb", 20 }, { "libkas3_program_bbbbbbbbbbbbbbbb.dylib", 21 },
		{ "cccccccccccccccc", 30 }, { "cccccccccccccccc.failed", 31 },
		{ "dddddddddddddddd", 5 }, { "libkas3_program_dddddddddddddddd.dylib", 6 },
		{ "eeeeeeeeeeeeeeee", 1 }, { "eeeeeeeeeeeeeeee.building", 2 },
		{ "notes.txt", 0 },
	};
	// `d` is in use, so it stays though it is old; of the rest the two newest (c, b) stay.
	std::vector<std::string> gone = play::stale(entries, { "dddddddddddddddd" }, 2);
	std::sort(gone.begin(), gone.end());
	const std::vector<std::string> wanted = { "aaaaaaaaaaaaaaaa", "eeeeeeeeeeeeeeee", "eeeeeeeeeeeeeeee.building", "libkas3_program_aaaaaaaaaaaaaaaa.dylib" };
	CHECK(gone == wanted);
	CHECK(play::stale(entries, { "dddddddddddddddd" }, 4).empty());
	CHECK(play::stale({}, {}, 0).empty());
	// A build is as new as the newest of its files: a library used again (its folder touched) is kept.
	const std::vector<play::Entry> touched = { { "aaaaaaaaaaaaaaaa", 100 }, { "libkas3_program_aaaaaaaaaaaaaaaa.dylib", 1 },
		{ "bbbbbbbbbbbbbbbb", 50 }, { "libkas3_program_bbbbbbbbbbbbbbbb.dylib", 50 } };
	gone = play::stale(touched, {}, 1);
	std::sort(gone.begin(), gone.end());
	CHECK(gone == (std::vector<std::string>{ "bbbbbbbbbbbbbbbb", "libkas3_program_bbbbbbbbbbbbbbbb.dylib" }));
}

// The command builds into a file of its own and gives it the library's name only when it is whole; a failure
// leaves the marker; a path with a space or a quote in it is one word to the shell.
static void test_script() {
	CHECK_EQ(play::shell_word("plain"), std::string("'plain'"));
	CHECK_EQ(play::shell_word("a b"), std::string("'a b'"));
	CHECK_EQ(play::shell_word("it's"), std::string("'it'\\''s'"));
	play::Build build;
	build.folder = "/games/my game/.godot/kas3";
	build.key = "0123456789abcdef";
	build.library = "libkas3_program_0123456789abcdef.dylib";
	build.compiler = "/usr/bin/c++";
	build.flags = "-O2 -shared";
	build.nice = "/usr/bin/nice";
	const std::string script = play::build_script(build);
	CHECK(script.find("exec < /dev/null > /dev/null 2>&1\n") == 0);
	CHECK(script.find("cd '/games/my game/.godot/kas3'") != std::string::npos);
	CHECK(script.find("echo $$ > '0123456789abcdef.building'") != std::string::npos);
	CHECK(script.find("'/usr/bin/nice' -n 10 '/usr/bin/c++' -O2 -shared") != std::string::npos);
	CHECK(script.find("-I '0123456789abcdef/kas3' '0123456789abcdef/program.cpp'") != std::string::npos);
	CHECK(script.find("-o 'libkas3_program_0123456789abcdef.dylib'.tmp.$$") != std::string::npos);
	CHECK(script.find("> '0123456789abcdef/build.log' 2>&1") != std::string::npos);
	CHECK(script.find("/bin/mv -f 'libkas3_program_0123456789abcdef.dylib'.tmp.$$ 'libkas3_program_0123456789abcdef.dylib'") != std::string::npos);
	CHECK(script.find("> '0123456789abcdef.failed'") != std::string::npos);
	CHECK(script.find("/bin/rm -f '0123456789abcdef.building'") != std::string::npos);
	build.nice.clear();
	CHECK(play::build_script(build).find("nice") == std::string::npos);
}

// The script does what it says, run by the shell: with a compiler that writes its output, the library is
// there and nothing else; with one that fails, the marker and the log are.
static void test_script_runs() {
	const std::filesystem::path folder = check::fresh_folder("play_script");
	const std::string key = "0123456789abcdef";
	std::filesystem::create_directories(folder / key);
	std::ofstream(folder / key / "program.cpp") << "int x;\n";
	std::ofstream(folder / play::building_name(key)) << "1\n";
	const std::filesystem::path good = folder / "good.sh";
	std::ofstream(good) << "#!/bin/sh\nwhile [ $# -gt 1 ]; do if [ \"$1\" = -o ]; then echo built > \"$2\"; fi; shift; done\necho warned >&2\n";
	const std::filesystem::path bad = folder / "bad.sh";
	std::ofstream(bad) << "#!/bin/sh\necho 'program.cpp:1: error: no' >&2\nexit 1\n";
	std::filesystem::permissions(good, std::filesystem::perms::owner_all);
	std::filesystem::permissions(bad, std::filesystem::perms::owner_all);
	play::Build build;
	build.folder = folder.string();
	build.key = key;
	build.library = "libkas3_program_" + key + ".dylib";
	build.compiler = good.string();
	build.flags = "-O2";
	CHECK_EQ(std::system(("/bin/sh -c " + play::shell_word(play::build_script(build))).c_str()), 0);
	CHECK(std::filesystem::exists(folder / build.library));
	CHECK(!std::filesystem::exists(folder / play::failed_name(key)));
	CHECK(!std::filesystem::exists(folder / play::building_name(key)));
	size_t files = 0;
	for (const auto &each : std::filesystem::directory_iterator(folder)) {
		files += each.path().filename().string().find(".tmp.") != std::string::npos;
	}
	CHECK_EQ(files, (size_t)0);
	std::filesystem::remove(folder / build.library);
	std::ofstream(folder / play::building_name(key)) << "1\n";
	build.compiler = bad.string();
	(void)std::system(("/bin/sh -c " + play::shell_word(play::build_script(build))).c_str());
	CHECK(!std::filesystem::exists(folder / build.library));
	CHECK(std::filesystem::exists(folder / play::failed_name(key)));
	CHECK(!std::filesystem::exists(folder / play::building_name(key)));
	std::ifstream log(folder / key / "build.log");
	std::string line;
	std::getline(log, line);
	CHECK_EQ(line, std::string("program.cpp:1: error: no"));
}

// The environment's word wins over the project's setting, either way; any other word leaves the setting.
static void test_switch() {
	CHECK(play::wanted(nullptr, true));
	CHECK(!play::wanted(nullptr, false));
	CHECK(!play::wanted("0", true));
	CHECK(!play::wanted("off", true));
	CHECK(!play::wanted("false", true));
	CHECK(play::wanted("1", false));
	CHECK(play::wanted("on", false));
	CHECK(play::wanted("true", false));
	CHECK(play::wanted("", true));
	CHECK(!play::wanted("", false));
	CHECK(play::wanted("perhaps", true));
	// With nothing said by either, Play builds nothing: native code is asked for.
	CHECK(!play::BY_DEFAULT);
}

// A build another run began is waited for only while it can still be going on.
static void test_building() {
	CHECK(play::still_building(1000, 1000));
	CHECK(play::still_building(1000, 1000 + play::BUILDING_SECONDS - 1));
	CHECK(!play::still_building(1000, 1000 + play::BUILDING_SECONDS));
	CHECK(play::still_building(1000, 990)); // Two clocks that differ by a little.
	CHECK(!play::still_building(1000, 1000, false)); // Whoever was building is gone: its marker counts for nothing.
}

// The compiler is the one named, or the first of the usual names on the PATH; none is none.
static void test_compiler_found() {
	const std::filesystem::path folder = check::fresh_folder("play_toolchain");
	const std::filesystem::path first = program(folder / "first", "g++");
	const std::filesystem::path second = program(folder / "second", "c++");
	program(folder / "second", "clang++");
	std::ofstream(folder / "second" / "plain.txt") << "x";
	toolchain::Search search;
	search.path = (folder / "none").string() + ":" + (folder / "first").string() + ":" + (folder / "second").string();
	// `c++` before `g++`, wherever each is on the PATH.
	std::optional<toolchain::Compiler> found = toolchain::find_compiler(search);
	CHECK(found.has_value());
	CHECK_EQ(found ? found->path : std::string(), second.string());
	CHECK(found && found->identity.find(second.string()) == 0);
	search.path = (folder / "first").string();
	found = toolchain::find_compiler(search);
	CHECK_EQ(found ? found->path : std::string(), first.string());
	search.path = "";
	CHECK(!toolchain::find_compiler(search));
	search.path = (folder / "none").string();
	CHECK(!toolchain::find_compiler(search));
	// One that is named is the one: by its path, or by its name on the PATH; and no other if it is not there.
	search.path = (folder / "first").string() + ":" + (folder / "second").string();
	search.named = "clang++";
	found = toolchain::find_compiler(search);
	CHECK_EQ(found ? found->path : std::string(), (folder / "second" / "clang++").string());
	search.named = first.string();
	found = toolchain::find_compiler(search);
	CHECK_EQ(found ? found->path : std::string(), first.string());
	search.named = (folder / "none" / "c++").string();
	CHECK(!toolchain::find_compiler(search));
	search.named = "no-such-compiler";
	CHECK(!toolchain::find_compiler(search));
	// A file that can't be run, and a folder, are no compiler.
	search.named = (folder / "second" / "plain.txt").string();
	CHECK(!toolchain::find_compiler(search));
	search.named = (folder / "second").string();
	CHECK(!toolchain::find_compiler(search));
}

// Where the system keeps programs that only stand for the developer tools' (macOS's /usr/bin): the compiler
// is there only if the tools are, and which it is is told by the tools' own.
static void test_compiler_stands_for_tools() {
	const std::filesystem::path folder = check::fresh_folder("play_shims");
	const std::filesystem::path shim = program(folder / "bin", "c++");
	toolchain::Search search;
	search.path = (folder / "bin").string();
	search.shims = (folder / "bin").string();
	search.developer = { "", (folder / "absent").string() };
	CHECK(!toolchain::find_compiler(search)); // No tools: the program there would only offer to install them.
	const std::filesystem::path tools = program(folder / "CommandLineTools" / "usr" / "bin", "clang");
	search.developer.push_back((folder / "CommandLineTools").string());
	std::optional<toolchain::Compiler> found = toolchain::find_compiler(search);
	CHECK_EQ(found ? found->path : std::string(), shim.string());
	CHECK(found && found->identity.find(tools.string()) == 0);
	const std::string before = found ? found->identity : std::string();
	// The tools brought up to date are another compiler, though the program that stands for them is the same.
	std::ofstream(tools, std::ios::app) << "# newer\n";
	found = toolchain::find_compiler(search);
	CHECK(found && found->identity != before);
	// The whole of Xcode keeps its compiler elsewhere.
	const std::filesystem::path xcode = program(folder / "Developer" / "Toolchains" / "XcodeDefault.xctoolchain" / "usr" / "bin", "clang");
	search.developer = { (folder / "Developer").string() };
	found = toolchain::find_compiler(search);
	CHECK(found && found->identity.find(xcode.string()) == 0);
	// One that is not among those programs is itself.
	const std::filesystem::path own = program(folder / "own", "c++");
	search.path = (folder / "own").string();
	search.developer.clear();
	found = toolchain::find_compiler(search);
	CHECK(found && found->identity.find(own.string()) == 0);
}

int main() {
	test_key();
	test_names();
	test_stale();
	test_script();
	test_script_runs();
	test_switch();
	test_building();
	test_compiler_found();
	test_compiler_stands_for_tools();
	return check::finish();
}
