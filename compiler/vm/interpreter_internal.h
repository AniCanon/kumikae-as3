// What the interpreter's own files share. Nothing outside vm/interpreter*.cpp includes this.
#pragma once

#include "interpreter.h"
#include "numeric.h"

namespace kas3 {
namespace vm {

// A value the interpreter must give up when its register, field or global is overwritten: a counted
// string or object, or a handle on an engine value.
inline bool held(ir::Kind p_kind) {
	return ir::is_reference(p_kind) || ir::is_engine(p_kind);
}

} // namespace vm
} // namespace kas3
