#include "program.h"

#include "builtins.h"
#include "callable.h"
#include "instance.h"
#include "lower.h"
#include "names.h"
#include "script.h"
#include "sources.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>

#include <unordered_map>
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
static uint64_t looks = 0;

uint64_t Project::watch_looks() {
	return looks;
}

uint64_t Project::generation() {
	return loads;
}

void Project::before_shutdown() {
	forget_scenes();
	free_instances_freed_elsewhere();
	free_detached();
	if (program && program->valid()) {
		release_listeners();
		program->interpreter().clear_globals();
		program->interpreter().collect(); // Loops holding Godot's objects go while Godot can free them.
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

// Flash stops a script that runs too long, after 15 seconds; so does this, after the project's setting
// `actionscript/limits/script_timeout_seconds` (0 for no limit).
static std::unique_ptr<vm::Interpreter> new_interpreter(const ir::Module &p_module, GodotHost &p_host) {
	p_host.new_program(); // What is worked out from here on is this program's.
	auto made = std::make_unique<vm::Interpreter>(p_module, p_host);
	const double seconds = ProjectSettings::get_singleton()->get_setting("actionscript/limits/script_timeout_seconds",
			vm::Interpreter::FLASH_TIME_LIMIT / 1000.0);
	made->set_time_limit(seconds > 0 ? (uint32_t)std::min(seconds * 1000.0, 4.0e9) : 0);
	return made;
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
	refresh_scenes(); // What they are now: an edit from here on is read again.
	std::string errors;
	if (!compile_project(program->module_.get(), &errors)) {
		UtilityFunctions::push_error(String("ActionScript: the project's scripts have errors and were not loaded.\n") +
				String::utf8(errors.c_str()));
		return *program;
	}
	if (const std::string library = NativeProgram::path(); !library.empty()) {
		std::string why;
		program->native_ = NativeProgram::load(library, *program->module_, program->host_, &why);
		if (program->native_) {
			UtilityFunctions::print("ActionScript: the scripts run as native code.");
		} else {
			UtilityFunctions::push_warning(String("ActionScript: the program library ") + String::utf8(library.c_str()) + " is not used (" +
					String::utf8(why.c_str()) + "): the interpreter runs the scripts.");
		}
	}
	if (!program->native_) {
		program->interpreter_ = new_interpreter(*program->module_, program->host_);
	}
	program->valid_ = program->interpreter().start();
	static bool watching = false;
	SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	if (!watching && tree && tree->get_root()) {
		watching = true;
		// Scripts are watched where a person can edit them: a project in a folder, run for development.
		if (OS::get_singleton()->is_debug_build() && DirAccess::dir_exists_absolute(ProjectSettings::get_singleton()->globalize_path(scripts_root()))) {
			tree->connect("process_frame", callable_mp_static(&Project::watch));
		}
	}
	return *program;
}

// How much the cycle collector looks at in a frame: objects and references. Most steps take 0.03 to 0.15
// ms; one that looks at a vector of 100,000 elements (an object is looked at whole), or that frees what a
// collection found, takes as long as that, 0.3 to 1.3 ms.
static const size_t COLLECTOR_WORK = 10000;

void Project::collect_step() {
	free_instances_freed_elsewhere();
	free_detached();
	if (program && program->valid_) {
		program->interpreter().collect_step(COLLECTOR_WORK);
	}
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
	looks++;
}

// What a scene started again would not make again, of the classes whose code that runs once changed: the
// Godot objects outside the scene that have their scripts, and objects of them that static fields reach.
static std::vector<std::string> kept_outside(const vm::Interpreter &p_running, const ir::Module &p_module,
		const std::vector<std::string> &p_classes) {
	std::vector<std::string> kept;
	const auto changed = [&](uint32_t p_class) {
		return std::find(p_classes.begin(), p_classes.end(), p_module.classes[p_class].name) != p_classes.end();
	};
	if (p_classes.empty()) {
		return kept;
	}
	SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	for (const auto &[object, owner] : instances_outside(tree ? tree->get_current_scene() : nullptr)) {
		const godot::Node *node = Object::cast_to<godot::Node>(owner);
		if (changed(object->klass)) {
			kept.push_back(String(node ? String(node->get_path()) : owner->to_string()).utf8().get_data());
		}
	}
	const std::vector<uint8_t> statics = vm::Reload::reached(p_running, {});
	for (uint32_t i = 0; i < statics.size(); i++) {
		if (statics[i] && changed(i)) {
			kept.push_back("objects of " + p_module.classes[i].name + " that static fields hold");
		}
	}
	return kept;
}

// Moves the running program onto a new build, the new program into `r_fresh`. A class of the library's
// containers that the new build no longer names, of which an object is alive, is added to the build. False,
// said, when an object alive has no place in the new program: the game carries on with the one it had.
static bool move_onto(vm::Interpreter &p_running, const ir::Module &p_old, GodotHost &p_host, ir::Module *r_module,
		std::unique_ptr<vm::Interpreter> *r_fresh, Restart *r_restart, const std::vector<std::string> &p_statics) {
	// Each try that fails for want of a container's class adds one of the old build's or more: it ends. (A
	// vector of vectors needs two: the inner one is met only once the outer one has its class.)
	for (;;) {
		auto fresh = new_interpreter(*r_module, p_host);
		std::string missing;
		std::vector<uint8_t> lost;
		{
			vm::Reload moving(p_running, *fresh);
			if (instances_can_move(moving) && listeners_can_move(moving) && moving.missing().empty()) {
				moving.globals(p_statics); // Their constants take the new first values; their variables keep theirs.
				move_instances(p_running, moving);
				move_listeners(p_running, moving, loads + 1);
				// A class whose code that runs once changed matters only if an object of it is alive.
				if (!moving.alive(r_restart->classes)) {
					r_restart->classes.clear();
					r_restart->kept.clear();
				}
				for (const std::string &name : p_statics) {
					if (moving.statics_given(name)) {
						r_restart->statics.push_back(name);
					}
				}
				*r_fresh = std::move(fresh);
				return true;
			}
			missing = moving.missing();
			lost = moving.missing_classes();
		}
		fresh->superseded(); // Godot's objects hold the running program's script objects, not this one's.
		fresh.reset();
		if (!vm::Reload::keep_containers(p_old, lost, r_module)) {
			UtilityFunctions::push_error(String("ActionScript: the scripts were not reloaded: an object of the class '") +
					String::utf8(missing.c_str()) + "' is alive, and the class is no longer there. Restart the game.");
			return false;
		}
	}
}

void Project::reload() {
	if (program && program->native_) {
		// Native code is not moved onto new code yet: the game carries on with what it was built from.
		UtilityFunctions::push_warning("ActionScript: the scripts changed, but they run as native code, which is not reloaded: build the "
									   "program library again and start the game again.");
		return;
	}
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
	free_instances_freed_elsewhere(); // Before the instances are looked through: their objects are gone.
	// Code that runs once (a constructor, a field's first value, `_ready`) does not run again for the
	// objects that are alive: for an edit there to show, the scene is made again from its file.
	Restart restart;
	restart.classes = ir::run_once_changes(*program->module_, *module);
	restart.kept = kept_outside(*program->interpreter_, *program->module_, restart.classes);
	std::unique_ptr<vm::Interpreter> fresh;
	if (!move_onto(*program->interpreter_, *program->module_, program->host_, module.get(), &fresh, &restart,
				ir::static_changes(*program->module_, *module))) {
		return;
	}
	loads++;
	// The old program goes last: what it still holds (its static fields) is let go while it can be.
	program->interpreter_.swap(fresh);
	program->module_.swap(module);
	fresh->superseded(); // Godot's objects now hold the new program's script objects, not the old one's.
	fresh.reset();
	module.reset();
	// The program just let go of was the only other one that held what the host worked out for it.
	program->host_.release_unused();
	if (std::getenv("KAS3_REPORT_PREPARED")) {
		const auto [kept, used] = program->host_.own_places();
		UtilityFunctions::print(String("ActionScript: ") + String::num_int64((int64_t)kept) + " calls keep a name of their own, " +
				String::num_int64((int64_t)used) + " of them the running program's");
	}
	AS3Script::refresh_all();
	UtilityFunctions::print(String("ActionScript: reloaded ") + String::num_int64((int64_t)count) + " scripts.");
	restart.scenes = refresh_scenes();
	restart_scene(restart);
}

void Project::reload_now() {
	if (program && program->valid_ && program->interpreter().depth() > 0) {
		UtilityFunctions::push_error("ActionScript: the scripts can't be reloaded while AS3 runs: the reload was not made.");
		return;
	}
	// Godot's debugger asks whenever the editor saves any script: nothing to do if no AS3 file changed.
	const uint64_t now = scripts_signature();
	if (program && program->valid_ && now == watch_loaded) {
		return;
	}
	watch_loaded = watch_seen = now; // What is read now: the watch need not reload it again.
	reload();
}

void Project::unload() {
	// First, so that whatever is destroyed along with the program (a listener held through one of its
	// static fields) knows the program is no longer there to be used.
	free_instances_freed_elsewhere();
	free_detached();
	if (program && program->valid_) {
		release_listeners(); // While the interpreter is still there to free what they hold.
		// And what the program holds, with loops among it: freeing a Godot object can call back into the
		// interpreter, which must still be there to be found.
		program->interpreter().clear_globals();
		program->interpreter().collect();
	}
	loads++;
	delete program;
	program = nullptr;
	forget_reached();
}

uint32_t Project::class_of_file(std::string_view p_path) const {
	// The class named after the file and declared in it: two folders may each have a Thing.as.
	const size_t slash = p_path.find_last_of('/');
	const size_t start = slash == std::string_view::npos ? 0 : slash + 1;
	const size_t end = p_path.rfind('.');
	const std::string_view stem = p_path.substr(start, end == std::string_view::npos || end < start ? std::string_view::npos : end - start);
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
