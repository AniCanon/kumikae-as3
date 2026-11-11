// Hand translation of as3/Empty.as.
#include "benches/shape.h"

namespace {

struct Empty : as3::Object {
	GDExtensionObjectPtr owner;
};
AS3_CLASS(Empty)

void Empty__process(Empty *self, double delta) {
	// Empty.as:5   (empty body: nothing can throw, so no frame)
}

double Empty_checksum(Empty *self) {
	// Empty.as:9   return 0;
	return 0.0;
}

AS3_THUNK_DELTA(Empty, _process)
AS3_THUNK_RET_NUMBER(Empty, checksum)

const as3::MethodEntry methods[] = {
	{ "_process", Empty_thunk__process },
	{ "checksum", Empty_thunk_checksum },
};
const as3::ScriptClass script_class = { "Empty", "Node2D", Empty_create, methods, 2, nullptr, 0 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
