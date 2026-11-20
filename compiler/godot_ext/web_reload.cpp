#include "web_reload.h"

#if defined(__EMSCRIPTEN__)
#include "program.h"

#include <godot_cpp/godot.hpp>

#include <optional>
#include <unordered_map>

namespace kas3 {

namespace {

std::unordered_map<std::string, std::string> &overrides() {
	static std::unordered_map<std::string, std::string> map;
	return map;
}

std::string &last_error() {
	static std::string message;
	return message;
}

} // namespace

const std::string *web_source_override(std::string_view p_res_path) {
	const auto &map = overrides();
	const auto found = map.find(std::string(p_res_path));
	return found != map.end() ? &found->second : nullptr;
}

} // namespace kas3

extern "C" GDE_EXPORT int kas3_web_reload(const char *p_res_path, const char *p_source) {
	if (!p_res_path || !p_source) {
		return 0;
	}
	auto &map = kas3::overrides();
	const std::string key(p_res_path);
	const auto prior = map.find(key);
	const std::optional<std::string> previous = prior != map.end() ? std::optional<std::string>(prior->second) : std::nullopt;
	map[key] = p_source;
	kas3::last_error().clear();
	if (!kas3::Project::reload_with_errors(&kas3::last_error())) {
		if (previous) {
			map[key] = *previous;
		} else {
			map.erase(key);
		}
		return 0;
	}
	return 1;
}

extern "C" GDE_EXPORT const char *kas3_web_last_error() {
	return kas3::last_error().c_str();
}

extern "C" GDE_EXPORT int kas3_web_reset_sources() {
	kas3::overrides().clear();
	kas3::last_error().clear();
	return kas3::Project::reload_with_errors(&kas3::last_error()) ? 1 : 0;
}
#endif
