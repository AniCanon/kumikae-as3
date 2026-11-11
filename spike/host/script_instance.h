// The object Godot holds for each node an AS3-shaped class is attached to.
#pragma once

#include "host/spike_script.h"

struct ScriptInstance {
	const ClassRuntime *runtime;
	as3::Object *object;
	GDExtensionObjectPtr owner;
	GDExtensionObjectPtr script;
};

// Creates the engine-side script instance for p_owner. Returns the engine's handle.
GDExtensionScriptInstancePtr create_script_instance(const SpikeScript *p_script, GDExtensionObjectPtr p_owner);
