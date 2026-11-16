// The built-in declaration files, embedded at build time from compiler/builtins/*.as.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace kas3 {

struct BuiltinFile {
	const char *path;
	const char *text;
};

extern const BuiltinFile BUILTIN_FILES[];
extern const int BUILTIN_FILE_COUNT;

// Godot's API as AS3 declarations for the `godot` package (compiler/godot/godot.as). It is not loaded with
// the built-ins: add it to a Compilation as a declaration file under GODOT_DECLARATIONS_PATH.
std::string godot_declarations();
inline const char *const GODOT_DECLARATIONS_PATH = "<godot>/godot.as";

// Godot's own spelling of what godot.as declares, for calling the engine: "move_and_slide" for
// ("CharacterBody2D", "moveAndSlide"), "Object" for the class "GodotObject" (an empty member). The class is
// the one that declares the member, not a subclass. Nothing if nothing of that name is declared. The names
// come from Godot's API description (compiler/godot/godot_names.txt), never from reversing the AS3 name.
std::optional<std::string> godot_name(std::string_view p_class, std::string_view p_member);
std::string godot_names_text();

} // namespace kas3
