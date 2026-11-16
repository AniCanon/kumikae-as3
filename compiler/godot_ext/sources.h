// Where the project's files are read from, and what they are now: for compiling them, and for watching them.
#pragma once

#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace kas3 {

// The folder the project's scripts are read from: "res://", or the one the game was told of.
godot::String scripts_root();
// A script by its place in the project ("res://game/Player.as"), wherever it is read from.
std::string project_path(const std::string &p_file, const godot::String &p_root);
// Every .as file under a folder, by where it is.
void collect_scripts(const godot::String &p_folder, std::vector<std::string> &r_files);
// What the project's scripts are now, as one number: it changes when one of them does.
uint64_t scripts_signature();

} // namespace kas3
