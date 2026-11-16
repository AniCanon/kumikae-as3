#include "sources.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
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

// A script by its place in the project ("res://game/Player.as"), wherever it is read from.
std::string project_path(const std::string &p_file, const String &p_root) {
	return "res://" + p_file.substr((size_t)p_root.utf8().length());
}

// Every .as file under a folder, by where it is. Folders and files whose name starts with a dot are not
// part of a project.
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
		if (!name.begins_with(".")) {
			collect_scripts(p_folder.path_join(name), r_files);
		}
	}
}

namespace {

// A folder as it was last listed: when it was last changed, and the files and folders in it.
struct Folder {
	bool listed = false;
	uint64_t changed = 0;
	std::vector<std::string> files; // The scripts.
	std::vector<String> folders;
};

std::unordered_map<std::string, Folder> &folders() {
	static std::unordered_map<std::string, Folder> all;
	return all;
}

// One look through the folder, for its scripts and its folders. Names that start with a dot are not part
// of a project.
void list_folder(const String &p_folder, Folder *r_folder) {
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
			r_folder->folders.push_back(p_folder.path_join(name));
		} else if (name.ends_with(".as")) {
			r_folder->files.push_back(p_folder.path_join(name).utf8().get_data());
		}
	}
	folder->list_dir_end();
}

// The scripts under a folder, by where they are, for the watch. Listing a folder is slow (on macOS, 20 to
// 50 microseconds a name), so a folder is listed again only when it changed (a file made, removed or
// renamed in it changes it) or changed in the last two seconds (its time is in whole seconds).
void watched_scripts(const String &p_folder, uint64_t p_now, std::vector<std::string> &r_files) {
	Folder &known = folders()[p_folder.utf8().get_data()];
	// By the folder's place on the disk: Godot gives "res://" itself no time (0, as for a folder in a pack).
	const uint64_t changed = FileAccess::get_modified_time(ProjectSettings::get_singleton()->globalize_path(p_folder));
	if (!known.listed || known.changed != changed || changed + 2 >= p_now) {
		list_folder(p_folder, &known);
		known.listed = true;
		known.changed = changed;
	}
	r_files.insert(r_files.end(), known.files.begin(), known.files.end());
	for (const String &inner : std::vector<String>(known.folders)) {
		watched_scripts(inner, p_now, r_files);
	}
}

} // namespace

// What the project's scripts are now: every file, when it was last written, and what it held when it was
// last read. The time is in whole seconds, so two saves within one are told apart only by what they hold:
// a file is read again while it was written in the last two seconds, and what it held then is kept, so
// that the signature does not change as the file grows old. (A file's size is not asked for: on macOS that
// opens the file, and took most of a look, 150 microseconds a script.)
uint64_t scripts_signature() {
	struct Seen {
		uint64_t written = 0, held = 0;
	};
	static std::unordered_map<std::string, Seen> seen;
	const uint64_t now = static_cast<uint64_t>(Time::get_singleton()->get_unix_time_from_system());
	std::vector<std::string> files;
	watched_scripts(scripts_root(), now, files);
	std::sort(files.begin(), files.end());
	uint64_t mixed = 1469598103934665603ull;
	const auto mix = [](uint64_t p_into, uint64_t p_value) { return (p_into ^ p_value) * 1099511628211ull; };
	for (const std::string &path : files) {
		for (char byte : path) {
			mixed = mix(mixed, (uint8_t)byte);
		}
		const String file = String::utf8(path.c_str());
		const uint64_t written = FileAccess::get_modified_time(file);
		Seen &known = seen[path];
		if (known.written != written || written + 2 >= now) {
			uint64_t held = 1469598103934665603ull;
			if (written + 2 >= now) {
				const PackedByteArray bytes = FileAccess::get_file_as_bytes(file);
				for (int64_t i = 0; i < bytes.size(); i++) {
					held = mix(held, bytes[i]);
				}
			}
			known = Seen{ written, held };
		}
		mixed = mix(mix(mixed, written), known.held);
	}
	return mixed;
}

} // namespace kas3
