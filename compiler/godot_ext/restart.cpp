// What a reload says of the objects alive, and the scene that is running made again from its file, so that
// what runs once runs again with the new code: when a person asks for it, or when the scene's own file was
// edited. A reload of the scripts never starts the scene again by itself.
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

void say_moved(const Moved &p_moved) {
	const vm::InPlace &done = p_moved.in_place;
	String line;
	if (!done.updated.empty()) {
		line = String("updated in place: ") + listed(done.updated) + ".";
	}
	if (!done.left.empty()) {
		String left;
		for (const auto &[name, why] : done.left) {
			left += String(left.is_empty() ? "" : ", ") + String::utf8(name.c_str()) + " (" + String::utf8(why.c_str()) + ")";
		}
		line += String(line.is_empty() ? "not updated in place: " : " Not updated: ") + left + ". Restart the scene to see the new code on those.";
	}
	if (!line.is_empty()) {
		UtilityFunctions::print(String("ActionScript: objects ") + line);
	}
	if (!p_moved.statics.empty()) {
		UtilityFunctions::print(String("ActionScript: the first values of static fields changed in ") + listed(p_moved.statics) +
				". Constants have their new values. Static variables keep the values the game gave them, through a reload and a "
				"scene started again; restart the game for their new first values.");
	}
}

void restart_scene(const Restart &p_restart) {
	if (in_flight) {
		// The scene being made now has some of the old build's code run: it is made again once it is in.
		once_more = true;
		UtilityFunctions::print(String("ActionScript: ") + (p_restart.asked ? "a restart was asked for" : "the scripts changed") +
				" while the scene was starting again; it starts once more when it is ready.");
		return;
	}
	SceneTree *tree = scene_tree();
	godot::Node *scene = tree ? tree->get_current_scene() : nullptr;
	const std::string file = scene ? std::string(scene->get_scene_file_path().utf8().get_data()) : std::string();
	if (!p_restart.asked) {
		if (!file.empty() && std::find(p_restart.scenes.begin(), p_restart.scenes.end(), file) != p_restart.scenes.end()) {
			UtilityFunctions::print("ActionScript: the scene's file changed, so the scene starts again.");
			in_flight = true;
			callable_mp_static(&start_again).call_deferred();
		}
		return;
	}
	if (file.empty()) {
		UtilityFunctions::print("ActionScript: there is no scene made from a file to start again.");
		return;
	}
	if (p_restart.classes.empty()) {
		UtilityFunctions::print("ActionScript: the scene starts again.");
	} else {
		UtilityFunctions::print(String("ActionScript: code that runs once changed in ") + listed(p_restart.classes) + ", so the scene starts again.");
	}
	if (!p_restart.kept.empty()) {
		UtilityFunctions::print(String("ActionScript: not made again, as they are kept outside the scene, and keeping what the old code did: ") +
				listed(p_restart.kept) + ". Restart the game to see it.");
	}
	in_flight = true;
	callable_mp_static(&start_again).call_deferred();
}

void forget_restart() {
	in_flight = false;
	once_more = false;
	SceneTree *tree = scene_tree();
	if (tree && tree->is_connected("scene_changed", callable_mp_static(&scene_changed))) {
		tree->disconnect("scene_changed", callable_mp_static(&scene_changed));
	}
}

} // namespace kas3
