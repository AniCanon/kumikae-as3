#include "program.h"

#include "builtins.h"
#include "callable.h"
#include "lower.h"
#include "names.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
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

// Every .as file of the project, by its place in it ("res://game/Player.as"). Folders and files whose
// name starts with a dot are not part of a project.
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

Project &Project::get() {
	if (!program) {
		program = new Project();
	}
	if (program->loaded_) {
		return *program;
	}
	program->loaded_ = true;
	loads++;
	std::vector<std::string> files;
	collect_scripts("res://", files);
	std::sort(files.begin(), files.end());

	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	for (const std::string &path : files) {
		// Read through Godot: in an exported game the project is inside a pack, not in a folder.
		const CharString source = FileAccess::get_file_as_string(String::utf8(path.c_str())).utf8();
		compilation.add(path, std::string(source.get_data(), (size_t)source.length()));
	}
	compilation.check();
	bool lowered = !compilation.has_errors() && ir::lower(compilation, &program->module_, ir::LowerMode::Library);
	if (!lowered || compilation.has_errors()) {
		UtilityFunctions::push_error(String("ActionScript: the project's scripts have errors and were not loaded.\n") +
				String::utf8(compilation.format_diagnostics().c_str()));
		return *program;
	}
	program->interpreter_ = std::make_unique<vm::Interpreter>(program->module_, program->host_);
	program->valid_ = program->interpreter_->start();
	static bool watching = false;
	SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	if (!watching && tree && tree->get_root()) {
		watching = true;
		tree->get_root()->connect("tree_exiting", callable_mp_static(&before_shutdown));
	}
	return *program;
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
	for (size_t i = 0; i < module_.classes.size(); i++) {
		const ir::Class &klass = module_.classes[i];
		const size_t dot = klass.name.rfind('.');
		if ((dot == std::string::npos ? klass.name : klass.name.substr(dot + 1)) != stem || klass.constructor == ir::NONE) {
			continue;
		}
		const uint32_t file = module_.functions[klass.constructor].file;
		if (file < module_.files.size() && module_.files[file] == p_path) {
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
