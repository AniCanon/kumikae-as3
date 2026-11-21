// In-memory script overrides for live WebAssembly hot reload from the host page.
#pragma once

#include <string>
#include <string_view>

namespace kas3 {

#if defined(__EMSCRIPTEN__) && !defined(KAS3_RUNTIME_ONLY)
// Returns overridden source text for a `res://...` script path, or null when reading from the pack.
const std::string *web_source_override(std::string_view p_res_path);
bool has_web_source_overrides();
#else
inline const std::string *web_source_override(std::string_view) {
	return nullptr;
}
inline bool has_web_source_overrides() {
	return false;
}
#endif

} // namespace kas3
