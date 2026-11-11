#include "benches/binds.h"

#include "runtime/godot_calls.h"

namespace binds {

GDExtensionMethodBindPtr Object_emit_signal = nullptr;
GDExtensionMethodBindPtr Object_set_meta = nullptr;
GDExtensionMethodBindPtr Object_get_meta = nullptr;
GDExtensionMethodBindPtr Node2D_get_position = nullptr;
GDExtensionMethodBindPtr Node2D_set_position = nullptr;
GDExtensionMethodBindPtr Node2D_get_rotation = nullptr;
GDExtensionMethodBindPtr Node2D_set_rotation = nullptr;
GDExtensionMethodBindPtr CanvasItem_get_world_2d = nullptr;
GDExtensionMethodBindPtr World2D_get_direct_space_state = nullptr;
GDExtensionMethodBindPtr PhysicsDirectSpaceState2D_intersect_ray = nullptr;
GDExtensionMethodBindPtr PhysicsRayQueryParameters2D_set_from = nullptr;
GDExtensionMethodBindPtr PhysicsRayQueryParameters2D_set_to = nullptr;
GDExtensionMethodBindPtr Line2D_set_points = nullptr;
GDExtensionMethodBindPtr Line2D_get_point_count = nullptr;

// Hashes come from the engine's extension_api.json (spike/api, dumped from Godot 4.7.2).
void init() {
	Object_emit_signal = as3::bind("Object", "emit_signal", 4047867050);
	Object_set_meta = as3::bind("Object", "set_meta", 3776071444);
	Object_get_meta = as3::bind("Object", "get_meta", 3990617847);
	Node2D_get_position = as3::bind("Node2D", "get_position", 3341600327);
	Node2D_set_position = as3::bind("Node2D", "set_position", 743155724);
	Node2D_get_rotation = as3::bind("Node2D", "get_rotation", 1740695150);
	Node2D_set_rotation = as3::bind("Node2D", "set_rotation", 373806689);
	CanvasItem_get_world_2d = as3::bind("CanvasItem", "get_world_2d", 2339128592);
	World2D_get_direct_space_state = as3::bind("World2D", "get_direct_space_state", 2506717822);
	PhysicsDirectSpaceState2D_intersect_ray = as3::bind("PhysicsDirectSpaceState2D", "intersect_ray", 1590275562);
	PhysicsRayQueryParameters2D_set_from = as3::bind("PhysicsRayQueryParameters2D", "set_from", 743155724);
	PhysicsRayQueryParameters2D_set_to = as3::bind("PhysicsRayQueryParameters2D", "set_to", 743155724);
	Line2D_set_points = as3::bind("Line2D", "set_points", 1509147220);
	Line2D_get_point_count = as3::bind("Line2D", "get_point_count", 3905245786);
}

} // namespace binds
