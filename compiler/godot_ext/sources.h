// Where the project's files are read from, and what they are now: for compiling them, and for watching them.
#pragma once

#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kas3 {
namespace ir { struct Module; }

// Lower a set of project scripts in the same order in a game and during export.
bool compile_scripts(std::vector<std::string> files, const godot::String &root, ir::Module *r_module, std::string *r_errors);
// Whether this run leaves the project-relative path out as part of a test folder.
bool excluded_script(std::string_view p_path);

// The folder the project's scripts are read from: "res://", or the one the game was told of.
godot::String scripts_root();
// A script by its place in the project ("res://game/Player.as"), wherever it is read from.
std::string project_path(std::string_view p_file, const godot::String &p_root);
// Every .as file under a folder, by where it is.
void collect_scripts(const godot::String &p_folder, std::vector<std::string> &r_files);
// What the project's scripts are now, as one number: it changes when one of them does. In a folder
// elsewhere, its scenes and resources saved as text (.tscn, .tres) are watched too.
uint64_t scripts_signature();
// In a folder elsewhere: the scenes and resources saved as text that changed since the last call (none at
// the first), read from there, each taking its place in the project (`res://...`) from the copy the game
// was built with, so that a scene started again, and whatever is loaded from then on, is the edited one.
// Their places in the project. Pictures and other imported files are not: they need Godot's editor to
// import them.
std::vector<std::string> refresh_scenes();
// Lets go of what refresh_scenes() read, while Godot can still free it.
void forget_scenes();

} // namespace kas3
