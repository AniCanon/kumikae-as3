#include "program.h"

#include "builtins.h"
#include "callable.h"
#include "lower.h"
#include "names.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <filesystem>
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

Project &Project::get() {
	if (!program) {
		program = new Project();
	}
	if (program->loaded_) {
		return *program;
	}
	program->loaded_ = true;
	loads++;
	std::string root = ProjectSettings::get_singleton()->globalize_path("res://").utf8().get_data();
	// Every .as file under the project, in a fixed order; folders whose name starts with a dot are not
	// part of a project.
	std::vector<std::string> files;
	std::error_code error;
	std::filesystem::recursive_directory_iterator at(root, std::filesystem::directory_options::skip_permission_denied, error);
	const std::filesystem::recursive_directory_iterator end;
	for (; !error && at != end; at.increment(error)) {
		std::string name = at->path().filename().string();
		if (at->is_directory(error)) {
			if (!name.empty() && name[0] == '.') {
				at.disable_recursion_pending();
			}
		} else if (at->path().extension() == ".as" && !name.empty() && name[0] != '.') {
			files.push_back(at->path().string());
		}
	}
	std::sort(files.begin(), files.end());

	Compilation compilation;
	compilation.add(GODOT_DECLARATIONS_PATH, godot_declarations(), true);
	for (const std::string &path : files) {
		std::string source;
		if (read_file(path, &source)) {
			compilation.add(path, std::move(source));
		}
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
	std::error_code error;
	std::filesystem::path wanted = std::filesystem::weakly_canonical(p_path, error);
	std::string stem = std::filesystem::path(p_path).stem().string();
	for (size_t i = 0; i < module_.classes.size(); i++) {
		const ir::Class &klass = module_.classes[i];
		size_t dot = klass.name.rfind('.');
		if ((dot == std::string::npos ? klass.name : klass.name.substr(dot + 1)) != stem || klass.constructor == ir::NONE) {
			continue;
		}
		uint32_t file = module_.functions[klass.constructor].file;
		if (file < module_.files.size() && std::filesystem::weakly_canonical(module_.files[file], error) == wanted) {
			return (uint32_t)i;
		}
	}
	return ir::NONE;
}

} // namespace kas3
