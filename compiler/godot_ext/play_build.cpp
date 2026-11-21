#include "play_build.h"

#if defined(__EMSCRIPTEN__) || defined(KAS3_RUNTIME_ONLY) || defined(__ANDROID__) || defined(IOS_ENABLED)

namespace kas3 {
namespace play {

std::string write_sources(const ir::Module &, const std::filesystem::path &, bool) {
#if defined(__EMSCRIPTEN__)
	return "native Play builds are not supported on the Web";
#elif defined(KAS3_RUNTIME_ONLY)
	return "native Play builds are not available in runtime-only builds";
#else
	return "native Play builds are not supported on mobile";
#endif
}

bool builds() {
	return false;
}

std::string library_for(const ir::Module &) {
	return {};
}

void build_ahead(const ir::Module &) {}
void poll() {}
void finish() {}

} // namespace play
} // namespace kas3

#else

#include "generate.h"
#include "native_host.h"
#include "play_cache.h"
#include "shared_library.h"
#include "toolchain.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <system_error>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <signal.h>
#endif

using namespace godot;

// The cache is the one place the extension reads and writes by the system's own paths: a C++ compiler is
// given them, and a game that runs from a pack builds nothing.
namespace fs = std::filesystem;

namespace kas3 {
namespace play {

namespace {

constexpr const char *SHELL = "/bin/sh";
constexpr uint64_t POLL_MILLIS = 250;

// One build asked for: where, under which key, by which compiler.
struct Asked {
	fs::path folder;
	std::string key;
	std::string library;
	toolchain::Compiler compiler;
	bool ahead = false; // For the next run only: nothing is said of it unless it fails.
};

// The scripts written as C++ for a build, on a thread of its own: a game that starts, and one whose
// scripts were just reloaded, does not wait for it. The module is a copy, the thread's alone; `done` is
// read by the main thread once the thread has been joined.
struct Writing {
	ir::Module module;
	Asked asked;
	std::thread thread;
	std::atomic<bool> ended{ false };
	bool done = false;
};
std::unique_ptr<Writing> writing;
// What to write when that has ended: the newest asked for meanwhile.
std::unique_ptr<Writing> writing_next;

// A compiler this run started and has not seen the end of.
struct Running {
	int32_t pid = -1;
	Asked asked;
	uint64_t started = 0; // In milliseconds.
};
Running running;
// The build to start when that one ends: the newest written meanwhile.
std::optional<Asked> waiting;
std::string loaded_key; // The build whose library this run loaded.
uint64_t polled = 0;

String text(const std::string &p_text) {
	return String::utf8(p_text.c_str());
}

std::string environment(const char *p_name) {
	return OS::get_singleton()->get_environment(p_name).utf8().get_data();
}

// The project's folder on this computer; none for a game that runs from a pack.
std::optional<fs::path> project_folder() {
	const fs::path folder(ProjectSettings::get_singleton()->globalize_path("res://").utf8().get_data());
	std::error_code failed;
	return !folder.empty() && fs::is_regular_file(folder / "project.godot", failed) ? std::optional<fs::path>(folder) : std::nullopt;
}

std::optional<toolchain::Compiler> compiler() {
	static const std::optional<toolchain::Compiler> found = [] {
		toolchain::Search search;
		search.named = environment("KAS3_CXX");
		search.path = environment("PATH");
#if defined(__APPLE__)
		search.shims = "/usr/bin";
		std::error_code failed;
		search.developer = { environment("DEVELOPER_DIR"), fs::read_symlink("/var/db/xcode_select_link", failed).string(),
			"/Applications/Xcode.app/Contents/Developer", "/Library/Developer/CommandLineTools" };
#endif
		return toolchain::find_compiler(search);
	}();
	return found;
}

void warn_no_compiler() {
	static bool said = false;
	if (said) {
		return;
	}
	said = true;
#if defined(__APPLE__)
	const char *install = "Install Xcode's command line tools (run `xcode-select --install` in a terminal)";
#else
	const char *install = "Install clang or g++";
#endif
	UtilityFunctions::push_warning(String("ActionScript: no C++ compiler was found, so the scripts run on the interpreter, which is slower. ") +
			install + " and the next run builds them as native code.");
}

int64_t seconds_now() {
	return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// When a file was written, in seconds of the system's clock: the file system's clock is another, and
// what the two differ by is asked now.
int64_t written(const fs::path &p_file) {
	std::error_code failed;
	const auto time = fs::last_write_time(p_file, failed);
	return failed ? 0 : seconds_now() - std::chrono::duration_cast<std::chrono::seconds>(fs::file_time_type::clock::now() - time).count();
}

bool write(const fs::path &p_file, std::string_view p_text) {
	std::ofstream file(p_file, std::ios::binary | std::ios::trunc);
	file.write(p_text.data(), static_cast<std::streamsize>(p_text.size()));
	return static_cast<bool>(file.flush());
}

std::string first_line(const fs::path &p_file) {
	std::ifstream file(p_file);
	std::string line;
	std::getline(file, line);
	return line;
}

// Whether the process a marker names (by its number) is still there.
bool alive(const std::string &p_process) {
#if defined(_WIN32)
	return !p_process.empty(); // Nothing is built there yet (builds()): the marker's age alone would say.
#else
	const long process = std::strtol(p_process.c_str(), nullptr, 10);
	return process > 0 && (::kill(static_cast<pid_t>(process), 0) == 0 || errno == EPERM);
#endif
}

// Everything of builds that are neither in use nor among the newest goes.
void clean(const fs::path &p_folder, const std::string &p_key) {
	std::vector<Entry> entries;
	std::error_code failed;
	for (const fs::directory_entry &each : fs::directory_iterator(p_folder, failed)) {
		entries.push_back(Entry{ each.path().filename().string(), written(each.path()) });
	}
	std::vector<std::string> kept = { p_key, loaded_key, running.asked.key };
	for (const Writing *each : { writing.get(), writing_next.get() }) {
		kept.push_back(each ? each->asked.key : std::string());
	}
	kept.push_back(waiting ? waiting->key : std::string());
	for (const std::string &name : stale(entries, kept, KEPT_BUILDS)) {
		fs::remove_all(p_folder / name, failed);
	}
}

void warn_failed(const Asked &p_asked) {
	const std::string why = first_line(p_asked.folder / failed_name(p_asked.key));
	const fs::path log = p_asked.folder / p_asked.key / "build.log";
	std::error_code failed;
	UtilityFunctions::push_warning(String("ActionScript: the scripts were not built as native code (") + text(why.empty() ? "the build failed" : why) +
			(fs::exists(log, failed) ? String("; what the compiler said is in ") + text(log.string()) : String()) +
			"): the interpreter runs them. This is no mistake of the scripts'.");
}

// The compiler started on what was written, beside the game. If it can't be, the marker says so.
void start(const Asked &p_asked) {
	const std::string nice = toolchain::runs("/usr/bin/nice") ? "/usr/bin/nice" : "";
	const Build build{ p_asked.folder.string(), p_asked.key, p_asked.library, p_asked.compiler.path, library_flags(), nice };
	// Said before it starts, for a run that begins meanwhile; the build names itself there, and takes it away as it ends.
	write(p_asked.folder / building_name(p_asked.key), std::to_string(OS::get_singleton()->get_process_id()) + "\n");
	PackedStringArray arguments;
	arguments.push_back("-c");
	arguments.push_back(text(build_script(build)));
	const int32_t pid = OS::get_singleton()->create_process(SHELL, arguments);
	if (pid <= 0) {
		write(p_asked.folder / failed_name(p_asked.key), "the C++ compiler could not be started\n");
		std::error_code failed;
		fs::remove(p_asked.folder / building_name(p_asked.key), failed);
		warn_failed(p_asked);
		return;
	}
	running = Running{ pid, p_asked, Time::get_singleton()->get_ticks_msec() };
}

void write_on_thread(std::unique_ptr<Writing> p_work) {
	writing = std::move(p_work);
	Writing *const work = writing.get();
	work->thread = std::thread([work] {
		const auto &asked = work->asked;
		const std::string error = write_sources(work->module, asked.folder / asked.key, true);
		work->done = error.empty();
		if (!work->done) {
			write(asked.folder / failed_name(asked.key), error + "\n");
		}
		work->ended.store(true, std::memory_order_release);
	});
}

// What a thread wrote is built, or said to have failed; then the thread writes what was asked for meanwhile.
// With p_wait, the thread is waited for (the game is ending) and the compilers are all started now.
void written_out(bool p_wait) {
	while (writing && (p_wait || writing->ended.load(std::memory_order_acquire))) {
		writing->thread.join();
		const std::unique_ptr<Writing> ended = std::move(writing);
		if (!ended->done) {
			warn_failed(ended->asked);
		} else if (running.pid > 0 && !p_wait) {
			waiting = ended->asked;
		} else {
			start(ended->asked);
		}
		if (writing_next) {
			write_on_thread(std::move(writing_next));
		}
	}
}

enum class Found { Library, Building, Failed, Nothing };

// What the cache has for a build: its library, a build of it going on, or its failure.
Found look(const Asked &p_asked) {
	std::error_code failed;
	if (fs::is_regular_file(p_asked.folder / p_asked.library, failed)) {
		return Found::Library;
	}
	if (fs::exists(p_asked.folder / failed_name(p_asked.key), failed)) {
		return Found::Failed;
	}
	const fs::path marker = p_asked.folder / building_name(p_asked.key);
	const bool ours = running.asked.key == p_asked.key || (waiting && waiting->key == p_asked.key) ||
			(writing && writing->asked.key == p_asked.key) || (writing_next && writing_next->asked.key == p_asked.key);
	const bool going = ours || (fs::exists(marker, failed) && still_building(written(marker), seconds_now(), alive(first_line(marker))));
	return going ? Found::Building : Found::Nothing;
}

std::optional<Asked> asked(const ir::Module &p_module) {
	const std::optional<fs::path> project = builds() ? project_folder() : std::nullopt;
	const std::optional<toolchain::Compiler> found = project ? compiler() : std::nullopt;
	if (!project) {
		return std::nullopt;
	}
	if (!found) {
		warn_no_compiler();
		return std::nullopt;
	}
	Asked out;
	out.folder = *project / ".godot" / "kas3";
	out.key = build_key(KeyParts{ ir::fingerprint(p_module), rt::RUNTIME_LAYOUT, BUILD_IDENTITY, found->identity, library_flags() });
	out.library = SharedLibrary::file_name(library_stem(out.key));
	out.compiler = *found;
	return out;
}

// Begins the build: the scripts are written on a thread, now or when the thread is free, and built once
// they are (written_out).
void begin(const ir::Module &p_module, const Asked &p_asked) {
	std::error_code failed;
	fs::create_directories(p_asked.folder, failed);
	clean(p_asked.folder, p_asked.key);
	std::unique_ptr<Writing> work = std::make_unique<Writing>();
	work->module = p_module;
	work->asked = p_asked;
	if (writing) {
		writing_next = std::move(work);
	} else {
		write_on_thread(std::move(work));
	}
}

} // namespace

std::string write_sources(const ir::Module &p_module, const fs::path &p_folder, bool p_remember) {
	backend_cpp::Options options;
	options.one_unit = true;
	options.remembers = p_remember;
	const auto generated = backend_cpp::generate(p_module, options);
	if (!generated.unsupported.empty()) {
		return "native code does not have this yet: " + generated.unsupported;
	}
	std::error_code failed;
	fs::create_directories(p_folder / HEADERS, failed);
	bool whole = !failed;
	for (const auto &file : generated.files) {
		whole = whole && write(p_folder / file.name, file.text);
	}
	for (size_t i = 0; i < RUNTIME_HEADER_COUNT; i++) {
		whole = whole && write(p_folder / HEADERS / RUNTIME_HEADERS[i].name, RUNTIME_HEADERS[i].text);
	}
	return whole ? std::string() : "the C++ could not be written into " + p_folder.string();
}

bool builds() {
#if (defined(__APPLE__) || defined(__linux__)) && !defined(__ANDROID__)
	static const bool here = [] {
		const String system = OS::get_singleton()->get_name();
		const bool setting = ProjectSettings::get_singleton()->get_setting("actionscript/native/play", play::BY_DEFAULT);
		const std::string said = environment("KAS3_NATIVE_PLAY");
		return (system == "macOS" || system == "Linux") && !Engine::get_singleton()->is_editor_hint() && wanted(said.c_str(), setting);
	}();
	return here;
#else
	return false;
#endif
}

std::string library_for(const ir::Module &p_module) {
	const std::optional<Asked> build = asked(p_module);
	if (!build) {
		return std::string();
	}
	switch (look(*build)) {
		case Found::Library: {
			std::error_code failed;
			fs::last_write_time(build->folder / build->key, fs::file_time_type::clock::now(), failed); // In use: kept.
			loaded_key = build->key;
			return (build->folder / build->library).string();
		}
		case Found::Nothing:
			begin(p_module, *build);
			UtilityFunctions::print("ActionScript: building the scripts as native code, in the background. This run uses the interpreter; the "
									"next one is native.");
			break;
		case Found::Building:
			UtilityFunctions::print("ActionScript: the scripts are still being built as native code. This run uses the interpreter.");
			break;
		case Found::Failed:
			warn_failed(*build);
			break;
	}
	return std::string();
}

void build_ahead(const ir::Module &p_module) {
	std::optional<Asked> build = asked(p_module);
	if (build && look(*build) == Found::Nothing) {
		build->ahead = true;
		begin(p_module, *build);
	}
}

void poll() {
	written_out(false);
	if (running.pid <= 0) {
		return;
	}
	const uint64_t now = Time::get_singleton()->get_ticks_msec();
	if (now - polled < POLL_MILLIS) {
		return;
	}
	polled = now;
	if (OS::get_singleton()->is_process_running(running.pid)) {
		return;
	}
	const Running ended = running;
	running = Running();
	std::error_code failed;
	if (fs::exists(ended.asked.folder / failed_name(ended.asked.key), failed)) {
		warn_failed(ended.asked);
	} else if (!ended.asked.ahead) {
		UtilityFunctions::print(String("ActionScript: the scripts are built as native code (") +
				String::num(static_cast<double>(now - ended.started) / 1000.0, 1) + " s): the next run uses it.");
	}
	if (waiting) {
		const Asked next = *waiting;
		waiting.reset();
		start(next);
	}
}

void finish() {
	written_out(true);
	if (waiting) {
		const Asked next = *waiting;
		waiting.reset();
		start(next);
	}
}

} // namespace play
} // namespace kas3
#endif
