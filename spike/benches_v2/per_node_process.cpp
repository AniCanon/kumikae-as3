// Hand translation of as3/PerNodeProcess.as and as3/PerNodeChild.as under the version 2 rules.
#include "benches_v2/shape2.h"

namespace {

const int32_t COUNT = 20000;

struct V2PerNodeChild : as3::Object {
	GDExtensionObjectPtr owner;
};
AS3_CLASS2(V2PerNodeChild)

void V2PerNodeChild__process(V2PerNodeChild *self, double delta) {
	// Nothing here can throw: every access is on `this` (null checks elided, rule 3) and every call is an
	// engine call (no error test, rule 4). So the function needs no site at all (rule 5).
	// PerNodeChild.as:6   rotation += delta;
	double rotation = as3::ptrcall_ret<double>(binds::Node2D_get_rotation, self->owner);
	rotation += delta;
	as3::ptrcall_void(binds::Node2D_set_rotation, self->owner, &rotation);
	// PerNodeChild.as:7   position.x += 10 * delta;
	godot::Vector2 p = as3::ptrcall_ret<godot::Vector2>(binds::Node2D_get_position, self->owner);
	p.x = (float)((double)p.x + 10.0 * delta);
	as3::ptrcall_void(binds::Node2D_set_position, self->owner, &p);
}

AS3_THUNK_DELTA(V2PerNodeChild, _process)

const as3::MethodEntry child_methods[] = {
	{ "_process", V2PerNodeChild_thunk__process },
};
const as3::ScriptClass child_class = { "V2PerNodeChild", "Node2D", V2PerNodeChild_create, child_methods, 1, nullptr, 0 };
const as3::AutoRegister child_register(&child_class);

struct V2PerNodeProcess : as3::Object {
	GDExtensionObjectPtr owner;
	as3::Ref<as3::Vector<GDExtensionObjectPtr>> children{ as3::Vector<GDExtensionObjectPtr>::create() };
};
AS3_CLASS2(V2PerNodeProcess)

void V2PerNodeProcess__ready(V2PerNodeProcess *self) {
	AS3_FN("PerNodeProcess/_ready", "PerNodeProcess.as");
	for (int32_t i = 0; i < COUNT; i = as3::add_int(i, 1)) {
		// PerNodeProcess.as:12-13
		GDExtensionObjectPtr child = as3::construct_object("Node2D");
		as3::attach_script(child, "V2PerNodeChild");
		as3::add_child(self->owner, child);
		// PerNodeProcess.as:14   children.push(child);
		AS3_CHECK_NULL_VOID(self->children.get(), 14);
		self->children->push(child);
	}
}

double V2PerNodeProcess_checksum(V2PerNodeProcess *self) {
	AS3_FN("PerNodeProcess/checksum", "PerNodeProcess.as");
	double sum = 0.0;
	AS3_CHECK_NULL(self->children.get(), 20);
	for (int32_t i = 0; i < self->children->length; i = as3::add_int(i, 1)) {
		as3::Vector<GDExtensionObjectPtr> *children = self->children.get();
		AS3_CHECK_NULL(children, 21);
		AS3_CHECK_BOUNDS(i, children->length, 21);
		GDExtensionObjectPtr child = children->data[i];
		AS3_CHECK_NULL(child, 22);
		double rotation = as3::ptrcall_ret<double>(binds::Node2D_get_rotation, child);
		godot::Vector2 p = as3::ptrcall_ret<godot::Vector2>(binds::Node2D_get_position, child);
		sum += rotation + (double)p.x;
		AS3_CHECK_NULL(self->children.get(), 20);
	}
	return sum;
}

AS3_THUNK_VOID(V2PerNodeProcess, _ready)
AS3_THUNK_RET_NUMBER(V2PerNodeProcess, checksum)

const as3::MethodEntry methods[] = {
	{ "_ready", V2PerNodeProcess_thunk__ready },
	{ "checksum", V2PerNodeProcess_thunk_checksum },
};
const as3::ScriptClass script_class = { "V2PerNodeProcess", "Node2D", V2PerNodeProcess_create, methods, 2, nullptr, 0 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
