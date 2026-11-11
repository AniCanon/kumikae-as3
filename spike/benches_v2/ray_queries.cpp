// Hand translation of as3/v2/RayQueries.as under the version 2 rules.
#include "benches_v2/shape2.h"

namespace {

const int32_t RAYS = 2000;

struct V2RayQueries : as3::Object {
	GDExtensionObjectPtr owner;
	as3::GodotRef params;
	int32_t hits = 0;
	double sumX = 0.0;
};
AS3_CLASS2(V2RayQueries)

// Scene construction. It runs once and is not measured, so it goes through the slow setup helpers.
void V2RayQueries__ready(V2RayQueries *self) {
	for (int32_t gy = 0; gy < 8; gy = as3::add_int(gy, 1)) {
		for (int32_t gx = 0; gx < 8; gx = as3::add_int(gx, 1)) {
			GDExtensionObjectPtr body = as3::construct_object("StaticBody2D");
			GDExtensionObjectPtr shape = as3::construct_object("CollisionShape2D");
			as3::GodotRef circle = as3::GodotRef::construct("CircleShape2D");
			as3::setup_set(circle.get(), "radius", 20.0);
			as3::setup_set(shape, "shape", as3::object_variant(circle.get()));
			as3::add_child(body, shape);
			godot::Vector2 at((float)(gx * 100 + 50), (float)(gy * 100 + 50));
			as3::ptrcall_void(binds::Node2D_set_position, body, &at);
			as3::add_child(self->owner, body);
		}
	}
	self->params = as3::GodotRef::construct("PhysicsRayQueryParameters2D");
}

void V2RayQueries__physics_process(V2RayQueries *self, double delta) {
	AS3_FN("RayQueries/_physics_process", "RayQueries.as");
	// RayQueries.as:37   var space:PhysicsDirectSpaceState2D = get_world_2d().direct_space_state;
	as3::GodotRef world =
			as3::GodotRef::adopt(as3::ptrcall_ret<GDExtensionObjectPtr>(binds::CanvasItem_get_world_2d, self->owner));
	AS3_CHECK_NULL_VOID(world.get(), 37);
	GDExtensionObjectPtr space =
			as3::ptrcall_ret<GDExtensionObjectPtr>(binds::World2D_get_direct_space_state, world.get());
	// The key of a constant index is built once (rule 7).
	static const godot::Variant key_position = godot::String("position");
	for (int32_t i = 0; i < RAYS; i = as3::add_int(i, 1)) {
		// RayQueries.as:39   params.from = new Vector2(-50, i % 800);
		GDExtensionObjectPtr params = self->params.get();
		AS3_CHECK_NULL_VOID(params, 39);
		godot::Vector2 from(-50.0f, (float)(i % 800));
		as3::ptrcall_void(binds::PhysicsRayQueryParameters2D_set_from, params, &from);
		// RayQueries.as:40   params.to = new Vector2(850, (i * 7) % 800);
		// The field is re-read and re-checked: the engine call above could have changed it (rule 3).
		params = self->params.get();
		AS3_CHECK_NULL_VOID(params, 40);
		godot::Vector2 to(850.0f, (float)(as3::mul_int(i, 7) % 800));
		as3::ptrcall_void(binds::PhysicsRayQueryParameters2D_set_to, params, &to);
		// RayQueries.as:41   var result:Dictionary = space.intersect_ray(params);
		// `space` is a local, checked once on first use and proven from then on (rule 3).
		AS3_CHECK_NULL_VOID(space, 41);
		params = self->params.get();
		godot::Dictionary result =
				as3::ptrcall_ret<godot::Dictionary>(binds::PhysicsDirectSpaceState2D_intersect_ray, space, &params);
		// RayQueries.as:42   if (!result.is_empty()) {   (the engine's dictionary, read in place: rule 7)
		if (!result.is_empty()) {
			// RayQueries.as:43   hits++;
			self->hits = as3::add_int(self->hits, 1);
			// RayQueries.as:44   var pos:Vector2 = result["position"];   (typed coercion checks the type)
			const godot::Variant &element = result[key_position];
			if (AS3_UNLIKELY(element.get_type() != godot::Variant::VECTOR2)) {
				AS3_SITE_(44);
				as3::throw_coercion_at(&as3_site_);
				return;
			}
			godot::Vector2 pos = element;
			// RayQueries.as:45   sumX += pos.x;
			self->sumX += (double)pos.x;
		}
	}
}

double V2RayQueries_checksum(V2RayQueries *self) {
	return (double)self->hits + self->sumX;
}

AS3_THUNK_VOID(V2RayQueries, _ready)
AS3_THUNK_DELTA(V2RayQueries, _physics_process)
AS3_THUNK_RET_NUMBER(V2RayQueries, checksum)

const as3::MethodEntry methods[] = {
	{ "_ready", V2RayQueries_thunk__ready },
	{ "_physics_process", V2RayQueries_thunk__physics_process },
	{ "checksum", V2RayQueries_thunk_checksum },
};
const as3::ScriptClass script_class = { "V2RayQueries", "Node2D", V2RayQueries_create, methods, 3, nullptr, 0 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
