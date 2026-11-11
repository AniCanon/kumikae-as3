// Every engine method the AS3-shaped classes call, fetched once at load (rule 9).
#pragma once

#include <gdextension_interface.h>

namespace binds {

extern GDExtensionMethodBindPtr Object_emit_signal;
extern GDExtensionMethodBindPtr Object_set_meta;
extern GDExtensionMethodBindPtr Object_get_meta;
extern GDExtensionMethodBindPtr Node2D_get_position;
extern GDExtensionMethodBindPtr Node2D_set_position;
extern GDExtensionMethodBindPtr Node2D_get_rotation;
extern GDExtensionMethodBindPtr Node2D_set_rotation;
extern GDExtensionMethodBindPtr CanvasItem_get_world_2d;
extern GDExtensionMethodBindPtr World2D_get_direct_space_state;
extern GDExtensionMethodBindPtr PhysicsDirectSpaceState2D_intersect_ray;
extern GDExtensionMethodBindPtr PhysicsRayQueryParameters2D_set_from;
extern GDExtensionMethodBindPtr PhysicsRayQueryParameters2D_set_to;
extern GDExtensionMethodBindPtr Line2D_set_points;
extern GDExtensionMethodBindPtr Line2D_get_point_count;

void init();

} // namespace binds
