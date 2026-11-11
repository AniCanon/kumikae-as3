// Hand translation of as3/PerNodeProcess.as and as3/PerNodeChild.as.
#include "benches/shape.h"

namespace {

const int32_t COUNT = 20000;

struct PerNodeChild : as3::Object {
	GDExtensionObjectPtr owner;
};
AS3_CLASS(PerNodeChild)

void PerNodeChild__process(PerNodeChild *self, double delta) {
	AS3_FRAME("PerNodeChild/_process", "PerNodeChild.as");
	// PerNodeChild.as:6   rotation += delta;
	AS3_LINE(6);
	double rotation = as3::ptrcall_ret<double>(binds::Node2D_get_rotation, self->owner);
	if (AS3_PENDING()) {
		return;
	}
	rotation += delta;
	as3::ptrcall_void(binds::Node2D_set_rotation, self->owner, &rotation);
	if (AS3_PENDING()) {
		return;
	}
	// PerNodeChild.as:7   position.x += 10 * delta;
	AS3_LINE(7);
	godot::Vector2 p = as3::ptrcall_ret<godot::Vector2>(binds::Node2D_get_position, self->owner);
	if (AS3_PENDING()) {
		return;
	}
	p.x = (float)((double)p.x + 10.0 * delta);
	as3::ptrcall_void(binds::Node2D_set_position, self->owner, &p);
	if (AS3_PENDING()) {
		return;
	}
}

AS3_THUNK_DELTA(PerNodeChild, _process)

const as3::MethodEntry child_methods[] = {
	{ "_process", PerNodeChild_thunk__process },
};
const as3::ScriptClass child_class = { "PerNodeChild", "Node2D", PerNodeChild_create, child_methods, 1, nullptr, 0 };
const as3::AutoRegister child_register(&child_class);

struct PerNodeProcess : as3::Object {
	GDExtensionObjectPtr owner;
	as3::Ref<as3::Vector<GDExtensionObjectPtr>> children{ as3::Vector<GDExtensionObjectPtr>::create() };
};
AS3_CLASS(PerNodeProcess)

void PerNodeProcess__ready(PerNodeProcess *self) {
	AS3_FRAME("PerNodeProcess/_ready", "PerNodeProcess.as");
	for (int32_t i = 0; i < COUNT; i = as3::add_int(i, 1)) {
		// PerNodeProcess.as:12   var child:Node2D = new PerNodeChild();
		AS3_LINE(12);
		GDExtensionObjectPtr child = as3::construct_object("Node2D");
		as3::attach_script(child, "PerNodeChild");
		if (AS3_PENDING()) {
			return;
		}
		// PerNodeProcess.as:13   add_child(child);
		AS3_LINE(13);
		as3::add_child(self->owner, child);
		if (AS3_PENDING()) {
			return;
		}
		// PerNodeProcess.as:14   children.push(child);
		AS3_LINE(14);
		AS3_NULLCHECK_VOID(self->children.get());
		self->children->push(child);
	}
}

double PerNodeProcess_checksum(PerNodeProcess *self) {
	AS3_FRAME("PerNodeProcess/checksum", "PerNodeProcess.as");
	double sum = 0.0;
	AS3_LINE(20);
	AS3_NULLCHECK(self->children.get());
	for (int32_t i = 0; i < self->children->length; i = as3::add_int(i, 1)) {
		// PerNodeProcess.as:21   var child:Node2D = children[i];
		AS3_LINE(21);
		as3::Vector<GDExtensionObjectPtr> *children = self->children.get();
		AS3_NULLCHECK(children);
		AS3_BOUNDS(children, i);
		GDExtensionObjectPtr child = children->data[i];
		// PerNodeProcess.as:22   sum += child.rotation + child.position.x;
		AS3_LINE(22);
		AS3_NULLCHECK(child);
		double rotation = as3::ptrcall_ret<double>(binds::Node2D_get_rotation, child);
		if (AS3_PENDING()) {
			return 0.0;
		}
		godot::Vector2 p = as3::ptrcall_ret<godot::Vector2>(binds::Node2D_get_position, child);
		if (AS3_PENDING()) {
			return 0.0;
		}
		sum += rotation + (double)p.x;
	}
	return sum;
}

AS3_THUNK_VOID(PerNodeProcess, _ready)
AS3_THUNK_RET_NUMBER(PerNodeProcess, checksum)

const as3::MethodEntry methods[] = {
	{ "_ready", PerNodeProcess_thunk__ready },
	{ "checksum", PerNodeProcess_thunk_checksum },
};
const as3::ScriptClass script_class = { "PerNodeProcess", "Node2D", PerNodeProcess_create, methods, 2, nullptr, 0 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
