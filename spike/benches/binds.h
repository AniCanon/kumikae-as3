// Every engine method the AS3-shaped classes call, fetched once at load (rule 9).
#pragma once

#include <gdextension_interface.h>

namespace binds {

extern GDExtensionMethodBindPtr Object_emit_signal;

void init();

} // namespace binds
