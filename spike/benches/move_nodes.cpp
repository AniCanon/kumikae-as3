// Hand translation of as3/MoveNodes.as.
#include "benches/shape.h"

namespace {

const int32_t COUNT = 20000;

struct MoveNodes : as3::Object {
	GDExtensionObjectPtr owner;
	as3::Ref<as3::Vector<GDExtensionObjectPtr>> children{ as3::Vector<GDExtensionObjectPtr>::create() };
	godot::Vector2 velocity{ 60.0f, 30.0f };
};
AS3_CLASS(MoveNodes)

void MoveNodes__ready(MoveNodes *self) {
	AS3_FRAME("MoveNodes/_ready", "MoveNodes.as");
	for (int32_t i = 0; i < COUNT; i = as3::add_int(i, 1)) {
		// MoveNodes.as:13   var child:Node2D = new Node2D();
		AS3_LINE(13);
		GDExtensionObjectPtr child = as3::construct_object("Node2D");
		// MoveNodes.as:14   child.position = new Vector2(i % 1000, int(i / 1000));
		AS3_LINE(14);
		godot::Vector2 start((float)(i % 1000), (float)(i / 1000));
		as3::ptrcall_void(binds::Node2D_set_position, child, &start);
		if (AS3_PENDING()) {
			return;
		}
		// MoveNodes.as:15   add_child(child);
		AS3_LINE(15);
		as3::add_child(self->owner, child);
		if (AS3_PENDING()) {
			return;
		}
		// MoveNodes.as:16   children.push(child);
		AS3_LINE(16);
		AS3_NULLCHECK_VOID(self->children.get());
		self->children->push(child);
	}
}

void MoveNodes__process(MoveNodes *self, double delta) {
	AS3_FRAME("MoveNodes/_process", "MoveNodes.as");
	// MoveNodes.as:21   var n:int = children.length;
	AS3_LINE(21);
	AS3_NULLCHECK_VOID(self->children.get());
	int32_t n = self->children->length;
	for (int32_t i = 0; i < n; i = as3::add_int(i, 1)) {
		// MoveNodes.as:23   var child:Node2D = children[i];
		// The field is re-read and re-checked each time round: the engine calls below could re-enter AS3.
		AS3_LINE(23);
		as3::Vector<GDExtensionObjectPtr> *children = self->children.get();
		AS3_NULLCHECK_VOID(children);
		AS3_BOUNDS_VOID(children, i);
		GDExtensionObjectPtr child = children->data[i];
		// MoveNodes.as:24   var p:Vector2 = child.position;
		AS3_LINE(24);
		AS3_NULLCHECK_VOID(child);
		godot::Vector2 p = as3::ptrcall_ret<godot::Vector2>(binds::Node2D_get_position, child);
		if (AS3_PENDING()) {
			return;
		}
		// MoveNodes.as:25   p = p + velocity * delta;
		p.x += self->velocity.x * (float)delta;
		p.y += self->velocity.y * (float)delta;
		// MoveNodes.as:26   if (p.x > 1000) p.x -= 1000;
		if ((double)p.x > 1000.0) {
			p.x = (float)((double)p.x - 1000.0);
		}
		// MoveNodes.as:27   child.position = p;   (child was proven non-null at line 24, same block)
		AS3_LINE(27);
		as3::ptrcall_void(binds::Node2D_set_position, child, &p);
		if (AS3_PENDING()) {
			return;
		}
	}
}

double MoveNodes_checksum(MoveNodes *self) {
	AS3_FRAME("MoveNodes/checksum", "MoveNodes.as");
	double sum = 0.0;
	AS3_LINE(33);
	AS3_NULLCHECK(self->children.get());
	for (int32_t i = 0; i < self->children->length; i = as3::add_int(i, 1)) {
		// MoveNodes.as:34   sum += children[i].position.x;
		AS3_LINE(34);
		as3::Vector<GDExtensionObjectPtr> *children = self->children.get();
		AS3_NULLCHECK(children);
		AS3_BOUNDS(children, i);
		GDExtensionObjectPtr child = children->data[i];
		AS3_NULLCHECK(child);
		godot::Vector2 p = as3::ptrcall_ret<godot::Vector2>(binds::Node2D_get_position, child);
		if (AS3_PENDING()) {
			return 0.0;
		}
		sum += (double)p.x;
	}
	return sum;
}

AS3_THUNK_VOID(MoveNodes, _ready)
AS3_THUNK_DELTA(MoveNodes, _process)
AS3_THUNK_RET_NUMBER(MoveNodes, checksum)

const as3::MethodEntry methods[] = {
	{ "_ready", MoveNodes_thunk__ready },
	{ "_process", MoveNodes_thunk__process },
	{ "checksum", MoveNodes_thunk_checksum },
};
const as3::ScriptClass script_class = { "MoveNodes", "Node2D", MoveNodes_create, methods, 3, nullptr, 0 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
