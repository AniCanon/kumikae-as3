// Hand translation of as3/MoveNodes.as under the version 2 rules.
#include "benches_v2/shape2.h"

namespace {

const int32_t COUNT = 20000;

struct V2MoveNodes : as3::Object {
	GDExtensionObjectPtr owner;
	as3::Ref<as3::Vector<GDExtensionObjectPtr>> children{ as3::Vector<GDExtensionObjectPtr>::create() };
	godot::Vector2 velocity{ 60.0f, 30.0f };
};
AS3_CLASS2(V2MoveNodes)

void V2MoveNodes__ready(V2MoveNodes *self) {
	AS3_FN("MoveNodes/_ready", "MoveNodes.as");
	for (int32_t i = 0; i < COUNT; i = as3::add_int(i, 1)) {
		// MoveNodes.as:13-15
		GDExtensionObjectPtr child = as3::construct_object("Node2D");
		godot::Vector2 start((float)(i % 1000), (float)(i / 1000));
		as3::ptrcall_void(binds::Node2D_set_position, child, &start);
		as3::add_child(self->owner, child);
		// MoveNodes.as:16   children.push(child);   (field re-read after engine calls: rule 3)
		AS3_CHECK_NULL_VOID(self->children.get(), 16);
		self->children->push(child);
	}
}

void V2MoveNodes__process(V2MoveNodes *self, double delta) {
	AS3_FN("MoveNodes/_process", "MoveNodes.as");
	// MoveNodes.as:21   var n:int = children.length;
	AS3_CHECK_NULL_VOID(self->children.get(), 21);
	int32_t n = self->children->length;
	for (int32_t i = 0; i < n; i = as3::add_int(i, 1)) {
		// MoveNodes.as:23   var child:Node2D = children[i];
		// Every pass makes engine calls, which count as able to replace or resize the vector, so the field,
		// its length and its data are re-read and re-checked each time (rules 3 and 7). Nothing is hoisted.
		as3::Vector<GDExtensionObjectPtr> *children = self->children.get();
		AS3_CHECK_NULL_VOID(children, 23);
		AS3_CHECK_BOUNDS_VOID(i, children->length, 23);
		GDExtensionObjectPtr child = children->data[i];
		// MoveNodes.as:24   var p:Vector2 = child.position;
		AS3_CHECK_NULL_VOID(child, 24);
		godot::Vector2 p = as3::ptrcall_ret<godot::Vector2>(binds::Node2D_get_position, child);
		// No error test after an engine call (rule 4).
		// MoveNodes.as:25   p = p + velocity * delta;
		p.x += self->velocity.x * (float)delta;
		p.y += self->velocity.y * (float)delta;
		// MoveNodes.as:26   if (p.x > 1000) p.x -= 1000;
		if ((double)p.x > 1000.0) {
			p.x = (float)((double)p.x - 1000.0);
		}
		// MoveNodes.as:27   child.position = p;   (null check elided: the local was checked at line 24, rule 3)
		as3::ptrcall_void(binds::Node2D_set_position, child, &p);
	}
}

double V2MoveNodes_checksum(V2MoveNodes *self) {
	AS3_FN("MoveNodes/checksum", "MoveNodes.as");
	double sum = 0.0;
	AS3_CHECK_NULL(self->children.get(), 33);
	for (int32_t i = 0; i < self->children->length; i = as3::add_int(i, 1)) {
		as3::Vector<GDExtensionObjectPtr> *children = self->children.get();
		AS3_CHECK_NULL(children, 34);
		AS3_CHECK_BOUNDS(i, children->length, 34);
		GDExtensionObjectPtr child = children->data[i];
		AS3_CHECK_NULL(child, 34);
		sum += (double)as3::ptrcall_ret<godot::Vector2>(binds::Node2D_get_position, child).x;
		AS3_CHECK_NULL(self->children.get(), 33);
	}
	return sum;
}

AS3_THUNK_VOID(V2MoveNodes, _ready)
AS3_THUNK_DELTA(V2MoveNodes, _process)
AS3_THUNK_RET_NUMBER(V2MoveNodes, checksum)

const as3::MethodEntry methods[] = {
	{ "_ready", V2MoveNodes_thunk__ready },
	{ "_process", V2MoveNodes_thunk__process },
	{ "checksum", V2MoveNodes_thunk_checksum },
};
const as3::ScriptClass script_class = { "V2MoveNodes", "Node2D", V2MoveNodes_create, methods, 3, nullptr, 0 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
