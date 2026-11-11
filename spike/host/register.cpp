#include "benches/binds.h"
#include "host/spike_language.h"
#include "host/spike_script.h"
#include "runtime/godot_calls.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

static void initialize(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(SpikeLanguage);
	GDREGISTER_CLASS(SpikeScript);
	as3::godot_calls_init();
	binds::init();
	SpikeLanguage::singleton = memnew(SpikeLanguage);
	Engine::get_singleton()->register_script_language(SpikeLanguage::singleton);
	UtilityFunctions::print("kas3_spike loaded");
}

static void terminate(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	SpikeScript::clear_cache();
	Engine::get_singleton()->unregister_script_language(SpikeLanguage::singleton);
	memdelete(SpikeLanguage::singleton);
	SpikeLanguage::singleton = nullptr;
}

extern "C" GDExtensionBool GDE_EXPORT kas3_spike_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
		GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init(p_get_proc_address, p_library, r_initialization);
	init.register_initializer(initialize);
	init.register_terminator(terminate);
	init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}
