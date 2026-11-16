#include "sources.h"

#include "test_folders.h"
#include "watch.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/time.hpp>

#include <algorithm>
#include <unordered_map>

using namespace godot;

namespace kas3 {

// Where the project's scripts are read from. The project itself, unless the game was told that a person
// edits them in a folder elsewhere: on a simulator the game runs from a pack inside the app, and the
// folder the scripts are written in is on the computer the simulator runs on.
String scripts_root() {
	if (OS::get_singleton()->is_debug_build()) {
		const String elsewhere = OS::get_singleton()->get_environment("KAS3_SOURCE_DIR");
		if (!elsewhere.is_empty() && DirAccess::dir_exists_absolute(elsewhere)) {
			return elsewhere.ends_with("/") ? elsewhere : elsewhere + String("/");
		}
	}
	return "res://";
}

// The folders of the project that hold its tests, which a game that is run does not read: none in a run
// that is the tests' own (it starts in one of them, or KAS3_TESTS says so). The project names them in its
// setting `actionscript/tests/folders`; `res://tests` where it names none. Worked out once: neither the
// setting nor what the run started with changes while it runs.
static const std::vector<std::string> &left_out() {
	static const std::vector<std::string> folders = []() {
		std::vector<std::string> named;
		const PackedStringArray set = ProjectSettings::get_singleton()->get_setting("actionscript/tests/folders", PackedStringArray({ "res://tests" }));
		for (const String &folder : set) {
			named.push_back(folder.utf8().get_data());
		}
		const std::vector<std::string> tests = test_places(named);
		std::vector<std::string> started = { String(ProjectSettings::get_singleton()->get_setting("application/run/main_scene", "")).utf8().get_data() };
		for (const String &word : OS::get_singleton()->get_cmdline_args()) {
			started.push_back(word.utf8().get_data());
		}
		const bool told = !OS::get_singleton()->get_environment("KAS3_TESTS").is_empty();
		return told || tests_run(started, tests) ? std::vector<std::string>() : tests;
	}();
	return folders;
}

// Whether a folder under the scripts' root is not part of this run's program: its name starts with a dot,
// or it holds the project's tests and the game is what runs.
static bool skipped(const String &p_folder, const String &p_name) {
	if (p_name.begins_with(".")) {
		return true;
	}
	const std::string place = p_folder.path_join(p_name).utf8().get_data();
	return in_test_folder(std::string_view(place).substr((size_t)scripts_root().utf8().length()), left_out());
}

// A script by its place in the project ("res://game/Player.as"), wherever it is read from.
std::string project_path(std::string_view p_file, const String &p_root) {
	return "res://" + std::string(p_file.substr((size_t)p_root.utf8().length()));
}

// Every .as file under a folder, by where it is. Folders and files whose name starts with a dot are not
// part of a project, and its tests' folders are not part of the game (skipped()).
void collect_scripts(const String &p_folder, std::vector<std::string> &r_files) {
	const Ref<DirAccess> folder = DirAccess::open(p_folder);
	if (folder.is_null()) {
		return;
	}
	for (const String &name : folder->get_files()) {
		if (name.ends_with(".as") && !name.begins_with(".")) {
			r_files.push_back(p_folder.path_join(name).utf8().get_data());
		}
	}
	for (const String &name : folder->get_directories()) {
		if (!skipped(p_folder, name)) {
			collect_scripts(p_folder.path_join(name), r_files);
		}
	}
}

namespace {

// A folder as it was last listed: when it was last changed, and the files and folders in it.
struct Folder {
	Seen seen; // When it was listed, by the time it was last changed.
	std::vector<std::string> files; // The scripts, and in a folder elsewhere its scenes and resources.
	std::vector<String> folders;
};

std::unordered_map<std::string, Folder> &folders() {
	static std::unordered_map<std::string, Folder> all;
	return all;
}

// A scene or a resource saved as text, which Godot reads as it is: no importing.
bool is_scene(const String &p_name) {
	return p_name.ends_with(".tscn") || p_name.ends_with(".tres");
}

bool is_scene(const std::string &p_path) {
	return is_scene(String::utf8(p_path.c_str()));
}

// One look through the folder, for its scripts (and, with `p_scenes`, its scenes) and its folders. Names
// that start with a dot are not part of a project.
void list_folder(const String &p_folder, bool p_scenes, Folder *r_folder) {
	r_folder->files.clear();
	r_folder->folders.clear();
	const Ref<DirAccess> folder = DirAccess::open(p_folder);
	if (folder.is_null()) {
		return;
	}
	folder->list_dir_begin();
	for (String name = folder->get_next(); !name.is_empty(); name = folder->get_next()) {
		if (name.begins_with(".")) {
			continue;
		}
		if (folder->current_is_dir()) {
			if (!skipped(p_folder, name)) {
				r_folder->folders.push_back(p_folder.path_join(name));
			}
		} else if (name.ends_with(".as") || (p_scenes && is_scene(name))) {
			r_folder->files.push_back(p_folder.path_join(name).utf8().get_data());
		}
	}
	folder->list_dir_end();
}

// The scripts under a folder (and its scenes, outside the project), by where they are, for the watch. Listing a folder is slow (on macOS, 20 to
// 50 microseconds a name), so a folder is listed again only when it changed (a file made, removed or
// renamed in it changes it) or may have changed again within the second of its time (watch.h).
void watched_files(const String &p_folder, uint64_t p_now, std::vector<std::string> &r_files) {
	Folder &known = folders()[p_folder.utf8().get_data()];
	// By the folder's place on the disk: Godot gives "res://" itself no time (0, as for a folder in a pack).
	const uint64_t changed = FileAccess::get_modified_time(ProjectSettings::get_singleton()->globalize_path(p_folder));
	look(known.seen, changed, p_now, [&]() {
		list_folder(p_folder, scripts_root() != "res://", &known);
		return uint64_t(0);
	});
	r_files.insert(r_files.end(), known.files.begin(), known.files.end());
	for (const String &inner : std::vector<String>(known.folders)) {
		watched_files(inner, p_now, r_files);
	}
}

std::unordered_map<std::string, Seen> &seen() {
	static std::unordered_map<std::string, Seen> all;
	return all;
}

// The scenes and resources read from the folder elsewhere, each in its place in the project.
std::unordered_map<std::string, Ref<Resource>> &taken() {
	static std::unordered_map<std::string, Ref<Resource>> all;
	return all;
}

} // namespace

// What the project's scripts are now: every file, when it was last written, and what it held when it was
// last read. The time is in whole seconds, so two saves within one are told apart only by what they hold:
// a file is read again for as long as another save of that time may have followed the last read
// (watch.h), and what it held then is kept. (A file's size is not asked for: on macOS that opens the file,
// and took most of a look, 150 microseconds a script.)
uint64_t scripts_signature() {
	const uint64_t now = static_cast<uint64_t>(Time::get_singleton()->get_unix_time_from_system());
	std::vector<std::string> files;
	watched_files(scripts_root(), now, files);
	std::sort(files.begin(), files.end());
	uint64_t mixed = 1469598103934665603ull;
	const auto mix = [](uint64_t p_into, uint64_t p_value) { return (p_into ^ p_value) * 1099511628211ull; };
	for (const std::string &path : files) {
		for (char byte : path) {
			mixed = mix(mixed, (uint8_t)byte);
		}
		const String file = String::utf8(path.c_str());
		const uint64_t written = FileAccess::get_modified_time(file);
		const uint64_t content = look(seen()[path], written, now, [&]() {
			uint64_t held = 1469598103934665603ull;
			const PackedByteArray bytes = FileAccess::get_file_as_bytes(file);
			for (int64_t i = 0; i < bytes.size(); i++) {
				held = mix(held, bytes[i]);
			}
			return held;
		});
		mixed = mix(mix(mixed, written), content);
	}
	return mixed;
}

std::vector<std::string> refresh_scenes() {
	static std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> read; // As each was when last read.
	static bool primed = false;
	const String root = scripts_root();
	if (root == "res://") {
		return {};
	}
	std::vector<std::string> files;
	watched_files(root, static_cast<uint64_t>(Time::get_singleton()->get_unix_time_from_system()), files);
	std::vector<std::string> changed;
	for (const std::string &path : files) {
		const Seen &now = seen()[path];
		auto &last = read[path];
		if (is_scene(path) && primed && last != std::make_pair(now.written, now.held)) {
			changed.push_back(path);
		}
		last = { now.written, now.held };
	}
	primed = true;
	// Resources first: a scene read after them finds them in their places.
	std::stable_sort(changed.begin(), changed.end(), [](const std::string &p_a, const std::string &p_b) {
		return String::utf8(p_a.c_str()).ends_with(".tres") && !String::utf8(p_b.c_str()).ends_with(".tres");
	});
	std::vector<std::string> places;
	for (const std::string &path : changed) {
		const Ref<Resource> fresh = ResourceLoader::get_singleton()->load(String::utf8(path.c_str()), "", ResourceLoader::CACHE_MODE_IGNORE);
		if (fresh.is_null()) {
			continue; // Godot has said why.
		}
		const std::string place = project_path(path, root);
		fresh->take_over_path(String::utf8(place.c_str()));
		taken()[place] = fresh;
		places.push_back(place);
	}
	return places;
}

void forget_scenes() {
	taken().clear();
}

} // namespace kas3
