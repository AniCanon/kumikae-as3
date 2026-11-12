#include "program.h"

#include "builtins.h"
#include "lower.h"
#include "names.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <filesystem>
#include <vector>

using namespace godot;

namespace kas3 {

static Project *program = nullptr;

Project &Project::get() {
	if (!program) {
		program = new Project();
	}
	if (program->loaded_) {
		return *program;
	}
	program->loaded_ = true;
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
	return *program;
}

void Project::unload() {
	delete program;
	program = nullptr;
}

uint32_t Project::class_of_file(const std::string &p_path) const {
	std::string stem = std::filesystem::path(p_path).stem().string();
	for (size_t i = 0; i < module_.classes.size(); i++) {
		const std::string &name = module_.classes[i].name;
		size_t dot = name.rfind('.');
		if ((dot == std::string::npos ? name : name.substr(dot + 1)) == stem) {
			return (uint32_t)i;
		}
	}
	return ir::NONE;
}

} // namespace kas3
