// A method of a script object as a function Godot can call: what a script hands over when it writes
// `timer.timeout.add(onTimeout)`.
#pragma once

#include "runtime.h"

#include <godot_cpp/variant/variant.hpp>

namespace kas3 {

// A new Callable for the method, as a Variant on the heap. The script object is kept alive for as long as
// the callable lives. p_owner is the Godot object the script object is attached to, or null: Godot drops
// the callable's connections when that object is freed, and a callable kept elsewhere then refuses to run.
godot::Variant *make_callable(vm::Object *p_object, uint32_t p_function, godot::Object *p_owner);

} // namespace kas3
