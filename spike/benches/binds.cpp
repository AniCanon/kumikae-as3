#include "benches/binds.h"

#include "runtime/godot_calls.h"

namespace binds {

GDExtensionMethodBindPtr Object_emit_signal = nullptr;

void init() {
	Object_emit_signal = as3::bind("Object", "emit_signal", 4047867050);
}

} // namespace binds
