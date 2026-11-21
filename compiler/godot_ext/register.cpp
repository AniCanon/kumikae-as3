// The extension's entry point: registers ActionScript 3 as a Godot scripting language.
#include "ios_export.h"
#include "language.h"
#include "play_build.h"
#include "program.h"
#include "script.h"

#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#if defined(__EMSCRIPTEN__)
#include <functional>
#if defined(_LIBCPP_AVAILABILITY_HAS_HASH_MEMORY) && _LIBCPP_AVAILABILITY_HAS_HASH_MEMORY
// Godot 4.7's Web export template is built with Emscripten 4.0.20 (LLVM 20), whose libc++ dylib does not
// export std::__hash_memory (added in LLVM 21). Provide it locally so newer Emscripten toolchains link.
namespace std {
inline namespace _LIBCPP_ABI_NAMESPACE {
[[__gnu__::__pure__]] _LIBCPP_EXPORTED_FROM_ABI size_t __hash_memory(_LIBCPP_NOESCAPE const void *p_ptr, size_t p_size) _NOEXCEPT {
	return __murmur2_or_cityhash<size_t>()(p_ptr, p_size);
}
} // namespace _LIBCPP_ABI_NAMESPACE
} // namespace std
#endif
#endif

using namespace godot;

AS3Language *AS3Language::singleton = nullptr;
static Ref<AS3ScriptLoader> loader;

static void initialize(ModuleInitializationLevel p_level) {
#if !defined(IOS_ENABLED) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(KAS3_RUNTIME_ONLY)
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_INTERNAL_CLASS(AS3IOSExport);
		GDREGISTER_INTERNAL_CLASS(AS3ExportEditor);
		EditorPlugins::add_by_type<AS3ExportEditor>();
	}
#endif
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	kas3::note_main_thread();
	GDREGISTER_CLASS(AS3Language);
	GDREGISTER_CLASS(AS3Script);
	GDREGISTER_CLASS(AS3ScriptLoader);
	AS3Language::singleton = memnew(AS3Language);
	Engine::get_singleton()->register_script_language(AS3Language::singleton);
	loader.instantiate();
	ResourceLoader::get_singleton()->add_resource_format_loader(loader);
}

static void terminate(ModuleInitializationLevel p_level) {
#if !defined(IOS_ENABLED) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(KAS3_RUNTIME_ONLY)
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorPlugins::remove_by_type<AS3ExportEditor>();
	}
#endif
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	ResourceLoader::get_singleton()->remove_resource_format_loader(loader);
	loader.unref();
	kas3::play::finish(); // Its thread, if the shutdown before this did not come.
	kas3::Project::unload();
	kas3::forget_defaults();
	kas3::forget_class_text();
	kas3::forget_class_signals();
	kas3::forget_folders();
	Engine::get_singleton()->unregister_script_language(AS3Language::singleton);
	memdelete(AS3Language::singleton);
	AS3Language::singleton = nullptr;
}

extern "C" GDExtensionBool GDE_EXPORT kas3_godot_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
		GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init(p_get_proc_address, p_library, r_initialization);
	init.register_initializer(initialize);
	init.register_terminator(terminate);
	init.register_shutdown_callback(&kas3::Project::before_shutdown);
	init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}
