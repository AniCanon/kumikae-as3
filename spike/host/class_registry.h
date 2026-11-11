// The table of AS3-shaped classes the host can attach to Godot objects.
#pragma once

#include "runtime/as3rt.h"

#include <gdextension_interface.h>

namespace as3 {

struct MethodEntry {
	const char *name;
	// Unboxes the engine's arguments and calls the typed method.
	void (*thunk)(Object *p_self, const GDExtensionConstVariantPtr *p_args, int64_t p_argc,
			GDExtensionVariantPtr r_ret);
};

struct ScriptClass {
	const char *name;
	const char *godot_base;
	Object *(*create)(GDExtensionObjectPtr p_owner);
	const MethodEntry *methods;
	int method_count;
	const char *const *signals;
	int signal_count;
};

void register_class(const ScriptClass *p_class);
const ScriptClass *find_class(const char *p_name);

struct AutoRegister {
	explicit AutoRegister(const ScriptClass *p_class) { register_class(p_class); }
};

} // namespace as3
