#include "program.h"

#include "builtins.h"
#include "callable.h"
#include "lower.h"
#include "names.h"
#include "script.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <thread>
#include <vector>

using namespace godot;

namespace kas3 {

static Project *program = nullptr;
static uint64_t loads = 0;
static const uint64_t WATCH_MILLIS = 200; // Between looks at the scripts.

uint64_t Project::generation() {
	return loads;
}

// The scene tree is being taken down: Godot is closing. What the program holds beyond the tree (in static
// fields, and through listeners) is let go now. Later, when this library is unloaded, other scripting
// languages are already gone, and a Godot object that one of their listeners is attached to can no longer
// be freed safely.
static void before_shutdown() {
	if (program && program->valid()) {
		release_listeners();
		program->interpreter().clear_globals();
	}
}

// Where the project's scripts are read from. The project itself, unless the game was told that a person
// edits them in a folder elsewhere: on a simulator the game runs from a pack inside the app, and the
// folder the scripts are written in is on the computer the simulator runs on.
static String scripts_root() {
	if (OS::get_singleton()->is_debug_build()) {
		const String elsewhere = OS::get_singleton()->get_environment("KAS3_SOURCE_DIR");
		if (!elsewhere.is_empty() && DirAccess::dir_exists_absolute(elsewhere)) {
			return elsewhere.ends_with("/") ? elsewhere : elsewhere + String("/");
		}
	}
	return "res://";
}

// A script by its place in the project ("res://game/Player.as"), wherever it is read from.
static std::string project_path(const std::string &p_file, const String &p_root) {
	return "res://" + p_file.substr((size_t)p_root.utf8().length());
}

// Every .as file under a folder, by where it is. Folders and files whose name starts with a dot are not
// part of a project.
static void collect_scripts(const String &p_folder, std::vector<std::string> &r_files) {
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

// Compiles every script of the project. False, with what is wrong, if they do not compile.
static bool compile_project(ir::Module *r_module, std::string *r_errors, size_t *r_count = nullptr) {
	const String root = scripts_root();
	std::vector<std::string> files;
	collect_scripts(root, files);
	std::sort(files.begin(), files.end());
	if (r_count) {
		*r_count = files.size();
	}
	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	for (const std::string &path : files) {
		// Read through Godot: in an exported game the project is inside a pack, not in a folder.
		const CharString source = FileAccess::get_file_as_string(String::utf8(path.c_str())).utf8();
		compilation.add(project_path(path, root), std::string(source.get_data(), (size_t)source.length()));
	}
	compilation.check();
	const bool lowered = !compilation.has_errors() && ir::lower(compilation, r_module, ir::LowerMode::Library);
	if (!lowered || compilation.has_errors()) {
		*r_errors = compilation.format_diagnostics();
		return false;
	}
	const std::string warnings = compilation.format_diagnostics();
	if (!warnings.empty()) {
		UtilityFunctions::push_warning(String("ActionScript:\n") + String::utf8(warnings.c_str()));
	}
	return true;
}

// What the project's scripts are now: every file and when it was last written.
static uint64_t scripts_signature() {
	std::vector<std::string> files;
	collect_scripts(scripts_root(), files);
	std::sort(files.begin(), files.end());
	uint64_t mixed = 1469598103934665603ull;
	for (const std::string &path : files) {
		for (char byte : path) {
			mixed = (mixed ^ (uint8_t)byte) * 1099511628211ull;
		}
		const String file = String::utf8(path.c_str());
		mixed = (mixed ^ FileAccess::get_modified_time(file)) * 1099511628211ull;
		// The time is in whole seconds: two saves within one are told apart by the size, when it differs.
		mixed = (mixed ^ (uint64_t)FileAccess::get_size(file)) * 1099511628211ull;
	}
	return mixed;
}

// What the running scripts were compiled from, and what was found at the last look.
static uint64_t watch_loaded = 0;
static uint64_t watch_seen = 0;

Project &Project::get() {
	if (!program) {
		program = new Project();
	}
	if (program->loaded_) {
		return *program;
	}
	program->loaded_ = true;
	loads++;
	// Taken before the scripts are read: an edit made from here on is one the game has not seen.
	watch_loaded = watch_seen = scripts_signature();
	std::string errors;
	if (!compile_project(program->module_.get(), &errors)) {
		UtilityFunctions::push_error(String("ActionScript: the project's scripts have errors and were not loaded.\n") +
				String::utf8(errors.c_str()));
		return *program;
	}
	program->interpreter_ = std::make_unique<vm::Interpreter>(*program->module_, program->host_);
	program->valid_ = program->interpreter_->start();
	static bool watching = false;
	SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	if (!watching && tree && tree->get_root()) {
		watching = true;
		tree->get_root()->connect("tree_exiting", callable_mp_static(&before_shutdown));
		// Scripts are watched where a person can edit them: a project in a folder, run for development.
		if (OS::get_singleton()->is_debug_build() && DirAccess::dir_exists_absolute(ProjectSettings::get_singleton()->globalize_path(scripts_root()))) {
			tree->connect("process_frame", callable_mp_static(&Project::watch));
		}
	}
	return *program;
}

void Project::watch() {
	static uint64_t looked = 0; // When, in milliseconds.
	const uint64_t time = Time::get_singleton()->get_ticks_msec();
	if (time - looked < WATCH_MILLIS) {
		return;
	}
	looked = time;
	const uint64_t now = scripts_signature();
	// An editor writes a file in steps. A change is acted on once it has stayed the same for one look.
	if (now != watch_loaded && now == watch_seen) {
		watch_loaded = now;
		reload();
	}
	watch_seen = now;
}

// After the code itself is swapped: the scene that is running is made again from its file, so that what
// runs once runs again with the new code. The game's static fields keep their values.
static void restart_scene(const std::vector<std::string> &p_classes) {
	String names;
	for (const std::string &name : p_classes) {
		names += String(names.is_empty() ? "" : ", ") + String::utf8(name.c_str());
	}
	SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	godot::Node *scene = tree ? tree->get_current_scene() : nullptr;
	if (!scene || scene->get_scene_file_path().is_empty()) {
		UtilityFunctions::print(String("ActionScript: code that runs once changed in ") + names +
				". Objects alive now keep what it did; restart the scene to see it.");
		return;
	}
	UtilityFunctions::print(String("ActionScript: code that runs once changed in ") + names + ", so the scene starts again.");
	tree->call_deferred("reload_current_scene");
}

void Project::reload() {
	if (!program || !program->valid_) {
		// Nothing is running: the scripts had errors. They are simply loaded again.
		unload();
		get();
		AS3Script::refresh_all();
		if (program->valid_) {
			UtilityFunctions::print("ActionScript: the scripts were loaded.");
		}
		return;
	}
	auto module = std::make_unique<ir::Module>();
	std::string errors;
	size_t count = 0;
	if (!compile_project(module.get(), &errors, &count)) {
		UtilityFunctions::push_error(String("ActionScript: the scripts have errors and were not reloaded. The game carries on with the ones it had.\n") +
				String::utf8(errors.c_str()));
		return;
	}
	// Code that runs once (a constructor, a field's first value, `_ready`) does not run again for the
	// objects that are alive: for an edit there to show, the scene is made again from its file.
	const std::vector<std::string> run_once = ir::run_once_changes(*program->module_, *module);
	bool restart = false;
	auto fresh = std::make_unique<vm::Interpreter>(*module, program->host_);
	{
		vm::Reload moving(*program->interpreter_, *fresh);
		if (!instances_can_move(moving) || !listeners_can_move(moving)) {
			UtilityFunctions::push_error(String("ActionScript: the scripts were not reloaded: an object of the class '") +
					String::utf8(moving.missing().c_str()) + "' is alive, and the class is no longer there. Restart the game.");
			return;
		}
		moving.globals();
		move_instances(*program->interpreter_, moving);
		move_listeners(*program->interpreter_, moving, loads + 1);
		// A class whose code that runs once changed matters only if an object of it is alive.
		restart = moving.alive(run_once);
	}
	loads++;
	// The old program goes last: what it still holds (its static fields) is let go while it can be.
	program->interpreter_.swap(fresh);
	program->module_.swap(module);
	fresh.reset();
	module.reset();
	AS3Script::refresh_all();
	UtilityFunctions::print(String("ActionScript: reloaded ") + String::num_int64((int64_t)count) + " scripts.");
	if (restart) {
		restart_scene(run_once);
	}
}

void Project::unload() {
	// First, so that whatever is destroyed along with the program (a listener held through one of its
	// static fields) knows the program is no longer there to be used.
	if (program && program->valid_) {
		release_listeners(); // While the interpreter is still there to free what they hold.
	}
	loads++;
	delete program;
	program = nullptr;
}

uint32_t Project::class_of_file(const std::string &p_path) const {
	// The class named after the file and declared in it: two folders may each have a Thing.as.
	const size_t slash = p_path.find_last_of('/');
	const size_t start = slash == std::string::npos ? 0 : slash + 1;
	const size_t end = p_path.rfind('.');
	const std::string stem = p_path.substr(start, end == std::string::npos || end < start ? std::string::npos : end - start);
	for (size_t i = 0; i < module_->classes.size(); i++) {
		const ir::Class &klass = module_->classes[i];
		const size_t dot = klass.name.rfind('.');
		if ((dot == std::string::npos ? klass.name : klass.name.substr(dot + 1)) != stem || klass.constructor == ir::NONE) {
			continue;
		}
		const uint32_t file = module_->functions[klass.constructor].file;
		if (file < module_->files.size() && module_->files[file] == p_path) {
			return (uint32_t)i;
		}
	}
	return ir::NONE;
}

static std::thread::id main_thread;

void note_main_thread() {
	main_thread = std::this_thread::get_id();
}

bool on_main_thread() {
	return std::this_thread::get_id() == main_thread;
}

void refuse_thread(uint32_t p_function) {
	// The names are only read, and the program is not replaced while the game's threads run.
	const ir::Module &module = Project::get().module();
	std::string name = p_function < module.functions.size() ? module.functions[p_function].name : std::string("a method");
	UtilityFunctions::push_error(String::utf8(("Error: " + name + "() was called by Godot from another thread. AS3 runs on the main thread only: the call was not made.").c_str()));
}

} // namespace kas3
