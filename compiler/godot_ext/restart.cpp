// After a reload whose code that runs once changed: the scene that is running is made again from its file,
// so that what runs once runs again with the new code.
#include "program.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>

using namespace godot;

namespace kas3 {

// From the deferred call to Godot's scene_changed. Godot takes the old scene out and makes the new one
// (its constructors run) in one step, and puts the new one in the tree (its `_ready` runs) in another.
static bool in_flight = false;
static bool once_more = false; // A reload came in that time.

static SceneTree *scene_tree() {
	return Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
}

static String listed(const std::vector<std::string> &p_names) {
	String out;
	for (const std::string &name : p_names) {
		out += String(out.is_empty() ? "" : ", ") + String::utf8(name.c_str());
	}
	return out;
}

static void start_again();

static void scene_changed() {
	in_flight = false;
	if (once_more) {
		once_more = false;
		UtilityFunctions::print("ActionScript: the scripts changed while the scene was starting again, so it starts once more.");
		in_flight = true;
		callable_mp_static(&start_again).call_deferred();
	}
}

static void start_again() {
	SceneTree *tree = scene_tree();
	if (tree && !tree->is_connected("scene_changed", callable_mp_static(&scene_changed))) {
		tree->connect("scene_changed", callable_mp_static(&scene_changed));
	}
	const Error error = tree && tree->get_current_scene() ? tree->reload_current_scene() : ERR_UNCONFIGURED;
	if (error != OK) {
		// Godot will not say the scene changed: nothing is in flight.
		in_flight = once_more = false;
		UtilityFunctions::push_error(String("ActionScript: the scene could not be started again (error ") + String::num_int64(error) +
				"). Objects alive now keep what the old code did; restart the scene to see it.");
	}
}

void restart_scene(const Restart &p_restart) {
	const String names = listed(p_restart.classes);
	if (!p_restart.statics.empty()) {
		UtilityFunctions::print(String("ActionScript: the first values of static fields changed in ") + listed(p_restart.statics) +
				". Static fields keep the values they were given, through a reload and a scene started again; restart the game for the new ones.");
	}
	if (in_flight) {
		// The scene being made now has some of the old build's code run: it is made again once it is in.
		once_more = true;
		UtilityFunctions::print("ActionScript: the scripts changed while the scene was starting again; it starts once more when it is ready.");
		return;
	}
	SceneTree *tree = scene_tree();
	godot::Node *scene = tree ? tree->get_current_scene() : nullptr;
	const std::string file = scene ? std::string(scene->get_scene_file_path().utf8().get_data()) : std::string();
	const bool edited = !file.empty() && std::find(p_restart.scenes.begin(), p_restart.scenes.end(), file) != p_restart.scenes.end();
	if (p_restart.classes.empty() && edited) {
		UtilityFunctions::print("ActionScript: the scene's file changed, so the scene starts again.");
		in_flight = true;
		callable_mp_static(&start_again).call_deferred();
		return;
	}
	if (p_restart.classes.empty()) {
		return;
	}
	if (!scene || scene->get_scene_file_path().is_empty()) {
		UtilityFunctions::print(String("ActionScript: code that runs once changed in ") + names +
				". Objects alive now keep what it did; restart the scene to see it.");
		return;
	}
	UtilityFunctions::print(String("ActionScript: code that runs once changed in ") + names + ", so the scene starts again.");
	if (!p_restart.kept.empty()) {
		UtilityFunctions::print(String("ActionScript: not made again, as they are kept outside the scene, and keeping what the old code did: ") +
				listed(p_restart.kept) + ". Restart the game to see it.");
	}
	in_flight = true;
	callable_mp_static(&start_again).call_deferred();
}

} // namespace kas3
