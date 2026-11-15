// The extension's entry point: registers ActionScript 3 as a Godot scripting language.
#include "language.h"
#include "program.h"
#include "script.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

AS3Language *AS3Language::singleton = nullptr;
static Ref<AS3ScriptLoader> loader;

static void initialize(ModuleInitializationLevel p_level) {
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
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	ResourceLoader::get_singleton()->remove_resource_format_loader(loader);
	loader.unref();
	kas3::Project::unload();
	kas3::forget_defaults();
	Engine::get_singleton()->unregister_script_language(AS3Language::singleton);
	memdelete(AS3Language::singleton);
	AS3Language::singleton = nullptr;
}

extern "C" GDExtensionBool GDE_EXPORT kas3_godot_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
		GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init(p_get_proc_address, p_library, r_initialization);
	init.register_initializer(initialize);
	init.register_terminator(terminate);
	init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}
