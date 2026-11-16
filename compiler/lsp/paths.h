// Paths as text, for any platform: the language server compares, joins and turns into URIs paths spelled
// one way, with '/' between their parts. The rules for Windows are here as well as those of the machine
// it runs on, so that both are tested everywhere.
#pragma once

#include <string>
#include <string_view>

namespace kas3 {

enum class PathStyle { Posix, Windows };

#ifdef _WIN32
constexpr PathStyle HOST_PATHS = PathStyle::Windows;
#else
constexpr PathStyle HOST_PATHS = PathStyle::Posix;
#endif

// The one spelling every comparison uses: '/' between the parts (on Windows a '\' is one too, and a drive's
// letter is a capital), no "." part, ".." taken back where there is a part to take, no doubled or trailing
// '/'. A root keeps its own: "/", "C:/", and a share's "//server/share".
std::string generic_path(std::string_view p_path, PathStyle p_style = HOST_PATHS);
// True if the path is the folder or something in it, both spelled as generic_path() spells them. On
// Windows letter case does not count.
bool path_within(std::string_view p_folder, std::string_view p_path, PathStyle p_style = HOST_PATHS);
// The name inside the folder: one '/' between them, also when the folder is a root.
std::string path_join(std::string_view p_folder, std::string_view p_name);

} // namespace kas3
