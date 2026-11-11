// Hand translation of as3/RayQueries.as.
#include "benches/shape.h"

namespace {

const int32_t RAYS = 2000;

struct RayQueries : as3::Object {
	GDExtensionObjectPtr owner;
	as3::GodotRef params;
	int32_t hits = 0;
	double sumX = 0.0;
};
AS3_CLASS(RayQueries)

// Scene construction. It runs once and is not measured, so it goes through the slow setup helpers.
void RayQueries__ready(RayQueries *self) {
	AS3_FRAME("RayQueries/_ready", "RayQueries.as");
	for (int32_t gy = 0; gy < 8; gy = as3::add_int(gy, 1)) {
		for (int32_t gx = 0; gx < 8; gx = as3::add_int(gx, 1)) {
			// RayQueries.as:21-28
			AS3_LINE(21);
			GDExtensionObjectPtr body = as3::construct_object("StaticBody2D");
			GDExtensionObjectPtr shape = as3::construct_object("CollisionShape2D");
			as3::GodotRef circle = as3::GodotRef::construct("CircleShape2D");
			AS3_LINE(24);
			as3::setup_set(circle.get(), "radius", 20.0);
			AS3_LINE(25);
			as3::setup_set(shape, "shape", as3::object_variant(circle.get()));
			AS3_LINE(26);
			as3::add_child(body, shape);
			AS3_LINE(27);
			godot::Vector2 at((float)(gx * 100 + 50), (float)(gy * 100 + 50));
			as3::ptrcall_void(binds::Node2D_set_position, body, &at);
			AS3_LINE(28);
			as3::add_child(self->owner, body);
			if (AS3_PENDING()) {
				return;
			}
		}
	}
	// RayQueries.as:31   params = new PhysicsRayQueryParameters2D();
	AS3_LINE(31);
	self->params = as3::GodotRef::construct("PhysicsRayQueryParameters2D");
}

void RayQueries__physics_process(RayQueries *self, double delta) {
	AS3_FRAME("RayQueries/_physics_process", "RayQueries.as");
	// RayQueries.as:35   var space:PhysicsDirectSpaceState2D = get_world_2d().direct_space_state;
	AS3_LINE(35);
	as3::GodotRef world =
			as3::GodotRef::adopt(as3::ptrcall_ret<GDExtensionObjectPtr>(binds::CanvasItem_get_world_2d, self->owner));
	if (AS3_PENDING()) {
		return;
	}
	AS3_NULLCHECK_VOID(world.get());
	GDExtensionObjectPtr space =
			as3::ptrcall_ret<GDExtensionObjectPtr>(binds::World2D_get_direct_space_state, world.get());
	if (AS3_PENDING()) {
		return;
	}
	static const godot::String key_position("position");
	for (int32_t i = 0; i < RAYS; i = as3::add_int(i, 1)) {
		// RayQueries.as:37   params.from = new Vector2(-50, i % 800);
		AS3_LINE(37);
		AS3_NULLCHECK_VOID(self->params.get());
		godot::Vector2 from(-50.0f, (float)(i % 800));
		as3::ptrcall_void(binds::PhysicsRayQueryParameters2D_set_from, self->params.get(), &from);
		if (AS3_PENDING()) {
			return;
		}
		// RayQueries.as:38   params.to = new Vector2(850, (i * 7) % 800);
		// Re-checked: the engine call above could have re-entered AS3 and cleared the field.
		AS3_LINE(38);
		AS3_NULLCHECK_VOID(self->params.get());
		godot::Vector2 to(850.0f, (float)(as3::mul_int(i, 7) % 800));
		as3::ptrcall_void(binds::PhysicsRayQueryParameters2D_set_to, self->params.get(), &to);
		if (AS3_PENDING()) {
			return;
		}
		// RayQueries.as:39   var result:Object = space.intersect_ray(params);
		// The returned Godot Dictionary is copied into an AS3 Object in full before anything reads it (rule 7).
		AS3_LINE(39);
		AS3_NULLCHECK_VOID(space);
		GDExtensionObjectPtr params_ptr = self->params.get();
		as3::Ref<as3::Object_> result = as3::from_godot_dictionary(as3::ptrcall_ret<godot::Dictionary>(binds::PhysicsDirectSpaceState2D_intersect_ray, space, &params_ptr));
		if (AS3_PENDING()) {
			return;
		}
		// RayQueries.as:40   if ("position" in result) {
		AS3_LINE(40);
		AS3_NULLCHECK_VOID(result.get());
		if (result->index.count(key_position) != 0) {
			// RayQueries.as:41   hits++;
			self->hits = as3::add_int(self->hits, 1);
			// RayQueries.as:42   var pos:Vector2 = result["position"];
			AS3_LINE(42);
			godot::Vector2 pos;
			if (AS3_UNLIKELY(!as3::value_to_vector2(result->get(key_position), &pos))) {
				as3::throw_error("TypeError", "Error #1034: Type Coercion failed: cannot convert to Vector2.");
				return;
			}
			// RayQueries.as:43   sumX += pos.x;
			self->sumX += (double)pos.x;
		}
	}
}

double RayQueries_checksum(RayQueries *self) {
	// RayQueries.as:49   return hits + sumX;
	return (double)self->hits + self->sumX;
}

AS3_THUNK_VOID(RayQueries, _ready)
AS3_THUNK_DELTA(RayQueries, _physics_process)
AS3_THUNK_RET_NUMBER(RayQueries, checksum)

const as3::MethodEntry methods[] = {
	{ "_ready", RayQueries_thunk__ready },
	{ "_physics_process", RayQueries_thunk__physics_process },
	{ "checksum", RayQueries_thunk_checksum },
};
const as3::ScriptClass script_class = { "RayQueries", "Node2D", RayQueries_create, methods, 3, nullptr, 0 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
