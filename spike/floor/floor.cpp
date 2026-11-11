// The floor: the same benchmarks as plain C++ through the same plugin interface, with none of the costs
// AS3 semantics add (no null or bounds checks, no error flag, no line bookkeeping, no reference counts,
// no collection conversion). It answers "what is the best any language could do on this route?".
// These are NOT translations of AS3 and are exempt from the translation rules.
#include "benches/shape.h"

#include <vector>

namespace {

// ---- move_nodes ----
struct FloorMoveNodes : as3::Object {
	GDExtensionObjectPtr owner;
	std::vector<GDExtensionObjectPtr> children;
	godot::Vector2 velocity{ 60.0f, 30.0f };
};
AS3_CLASS(FloorMoveNodes)

void FloorMoveNodes__ready(FloorMoveNodes *self) {
	for (int32_t i = 0; i < 20000; i++) {
		GDExtensionObjectPtr child = as3::construct_object("Node2D");
		godot::Vector2 start((float)(i % 1000), (float)(i / 1000));
		as3::ptrcall_void(binds::Node2D_set_position, child, &start);
		as3::add_child(self->owner, child);
		self->children.push_back(child);
	}
}
void FloorMoveNodes__process(FloorMoveNodes *self, double delta) {
	GDExtensionObjectPtr *children = self->children.data();
	size_t n = self->children.size();
	godot::Vector2 velocity = self->velocity;
	for (size_t i = 0; i < n; i++) {
		godot::Vector2 p = as3::ptrcall_ret<godot::Vector2>(binds::Node2D_get_position, children[i]);
		p.x += velocity.x * (float)delta;
		p.y += velocity.y * (float)delta;
		if ((double)p.x > 1000.0) {
			p.x = (float)((double)p.x - 1000.0);
		}
		as3::ptrcall_void(binds::Node2D_set_position, children[i], &p);
	}
}
double FloorMoveNodes_checksum(FloorMoveNodes *self) {
	double sum = 0.0;
	for (GDExtensionObjectPtr child : self->children) {
		sum += (double)as3::ptrcall_ret<godot::Vector2>(binds::Node2D_get_position, child).x;
	}
	return sum;
}
AS3_THUNK_VOID(FloorMoveNodes, _ready)
AS3_THUNK_DELTA(FloorMoveNodes, _process)
AS3_THUNK_RET_NUMBER(FloorMoveNodes, checksum)
const as3::MethodEntry move_methods[] = {
	{ "_ready", FloorMoveNodes_thunk__ready },
	{ "_process", FloorMoveNodes_thunk__process },
	{ "checksum", FloorMoveNodes_thunk_checksum },
};
const as3::ScriptClass move_class = { "FloorMoveNodes", "Node2D", FloorMoveNodes_create, move_methods, 3, nullptr, 0 };
const as3::AutoRegister move_register(&move_class);

// ---- per_node_process ----
struct FloorPerNodeChild : as3::Object {
	GDExtensionObjectPtr owner;
};
AS3_CLASS(FloorPerNodeChild)
void FloorPerNodeChild__process(FloorPerNodeChild *self, double delta) {
	double rotation = as3::ptrcall_ret<double>(binds::Node2D_get_rotation, self->owner) + delta;
	as3::ptrcall_void(binds::Node2D_set_rotation, self->owner, &rotation);
	godot::Vector2 p = as3::ptrcall_ret<godot::Vector2>(binds::Node2D_get_position, self->owner);
	p.x = (float)((double)p.x + 10.0 * delta);
	as3::ptrcall_void(binds::Node2D_set_position, self->owner, &p);
}
AS3_THUNK_DELTA(FloorPerNodeChild, _process)
const as3::MethodEntry child_methods[] = { { "_process", FloorPerNodeChild_thunk__process } };
const as3::ScriptClass child_class = { "FloorPerNodeChild", "Node2D", FloorPerNodeChild_create, child_methods, 1, nullptr, 0 };
const as3::AutoRegister child_register(&child_class);

struct FloorPerNodeProcess : as3::Object {
	GDExtensionObjectPtr owner;
	std::vector<GDExtensionObjectPtr> children;
};
AS3_CLASS(FloorPerNodeProcess)
void FloorPerNodeProcess__ready(FloorPerNodeProcess *self) {
	for (int32_t i = 0; i < 20000; i++) {
		GDExtensionObjectPtr child = as3::construct_object("Node2D");
		as3::attach_script(child, "FloorPerNodeChild");
		as3::add_child(self->owner, child);
		self->children.push_back(child);
	}
}
double FloorPerNodeProcess_checksum(FloorPerNodeProcess *self) {
	double sum = 0.0;
	for (GDExtensionObjectPtr child : self->children) {
		sum += as3::ptrcall_ret<double>(binds::Node2D_get_rotation, child) +
				(double)as3::ptrcall_ret<godot::Vector2>(binds::Node2D_get_position, child).x;
	}
	return sum;
}
AS3_THUNK_VOID(FloorPerNodeProcess, _ready)
AS3_THUNK_RET_NUMBER(FloorPerNodeProcess, checksum)
const as3::MethodEntry per_node_methods[] = {
	{ "_ready", FloorPerNodeProcess_thunk__ready },
	{ "checksum", FloorPerNodeProcess_thunk_checksum },
};
const as3::ScriptClass per_node_class = { "FloorPerNodeProcess", "Node2D", FloorPerNodeProcess_create, per_node_methods, 2, nullptr, 0 };
const as3::AutoRegister per_node_register(&per_node_class);

// ---- ray_queries: reads the one key straight out of the engine's dictionary ----
struct FloorRayQueries : as3::Object {
	GDExtensionObjectPtr owner;
	as3::GodotRef params;
	int32_t hits = 0;
	double sumX = 0.0;
};
AS3_CLASS(FloorRayQueries)
void FloorRayQueries__ready(FloorRayQueries *self) {
	for (int32_t gy = 0; gy < 8; gy++) {
		for (int32_t gx = 0; gx < 8; gx++) {
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
void FloorRayQueries__physics_process(FloorRayQueries *self, double delta) {
	as3::GodotRef world =
			as3::GodotRef::adopt(as3::ptrcall_ret<GDExtensionObjectPtr>(binds::CanvasItem_get_world_2d, self->owner));
	GDExtensionObjectPtr space =
			as3::ptrcall_ret<GDExtensionObjectPtr>(binds::World2D_get_direct_space_state, world.get());
	static const godot::Variant key_position = godot::String("position");
	GDExtensionObjectPtr params = self->params.get();
	for (int32_t i = 0; i < 2000; i++) {
		godot::Vector2 from(-50.0f, (float)(i % 800));
		as3::ptrcall_void(binds::PhysicsRayQueryParameters2D_set_from, params, &from);
		godot::Vector2 to(850.0f, (float)((i * 7) % 800));
		as3::ptrcall_void(binds::PhysicsRayQueryParameters2D_set_to, params, &to);
		godot::Dictionary result =
				as3::ptrcall_ret<godot::Dictionary>(binds::PhysicsDirectSpaceState2D_intersect_ray, space, &params);
		if (!result.is_empty()) {
			self->hits++;
			godot::Vector2 pos = result[key_position];
			self->sumX += (double)pos.x;
		}
	}
}
double FloorRayQueries_checksum(FloorRayQueries *self) {
	return (double)self->hits + self->sumX;
}
AS3_THUNK_VOID(FloorRayQueries, _ready)
AS3_THUNK_DELTA(FloorRayQueries, _physics_process)
AS3_THUNK_RET_NUMBER(FloorRayQueries, checksum)
const as3::MethodEntry ray_methods[] = {
	{ "_ready", FloorRayQueries_thunk__ready },
	{ "_physics_process", FloorRayQueries_thunk__physics_process },
	{ "checksum", FloorRayQueries_thunk_checksum },
};
const as3::ScriptClass ray_class = { "FloorRayQueries", "Node2D", FloorRayQueries_create, ray_methods, 3, nullptr, 0 };
const as3::AutoRegister ray_register(&ray_class);

// ---- signal_traffic ----
struct FloorSignalTraffic : as3::Object {
	GDExtensionObjectPtr owner;
	double total = 0.0;
};
AS3_CLASS(FloorSignalTraffic)
void FloorSignalTraffic__ready(FloorSignalTraffic *self) {
	as3::connect_method(self->owner, "hit", "on_hit");
}
void FloorSignalTraffic__process(FloorSignalTraffic *self, double delta) {
	static const godot::Variant hit_name = godot::StringName("hit");
	for (int32_t i = 0; i < 20000; i++) {
		godot::Variant amount((int64_t)i);
		godot::Variant pos(godot::Vector2((float)i, 1.0f));
		const godot::Variant *args[] = { &hit_name, &amount, &pos };
		as3::varcall(binds::Object_emit_signal, self->owner, args, 3);
	}
}
double FloorSignalTraffic_checksum(FloorSignalTraffic *self) {
	return self->total;
}
AS3_THUNK_VOID(FloorSignalTraffic, _ready)
AS3_THUNK_DELTA(FloorSignalTraffic, _process)
AS3_THUNK_RET_NUMBER(FloorSignalTraffic, checksum)
void FloorSignalTraffic_thunk_on_hit(as3::Object *p_self, const GDExtensionConstVariantPtr *p_args, int64_t,
		GDExtensionVariantPtr) {
	static_cast<FloorSignalTraffic *>(p_self)->total +=
			(double)as3::arg_int(p_args[0]) + (double)as3::arg_vector2(p_args[1]).x;
}
const as3::MethodEntry signal_methods[] = {
	{ "_ready", FloorSignalTraffic_thunk__ready },
	{ "_process", FloorSignalTraffic_thunk__process },
	{ "on_hit", FloorSignalTraffic_thunk_on_hit },
	{ "checksum", FloorSignalTraffic_thunk_checksum },
};
const char *const signal_names[] = { "hit" };
const as3::ScriptClass signal_class = { "FloorSignalTraffic", "Node2D", FloorSignalTraffic_create, signal_methods, 4, signal_names, 1 };
const as3::AutoRegister signal_register(&signal_class);

// ---- nbody: plain structs, plain loops ----
const double SOLAR_MASS = 4 * 3.141592653589793 * 3.141592653589793;
const double YEAR = 365.24;
struct PlainBody {
	double x, y, z, vx, vy, vz, mass;
};
struct FloorNBody : as3::Object {
	GDExtensionObjectPtr owner;
	std::vector<PlainBody *> bodies;
};
AS3_CLASS(FloorNBody)
void FloorNBody__ready(FloorNBody *self) {
	self->bodies.push_back(new PlainBody{ 0, 0, 0, 0, 0, 0, SOLAR_MASS });
	self->bodies.push_back(new PlainBody{ 4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
			1.66007664274403694e-03 * YEAR, 7.69901118419740425e-03 * YEAR, -6.90460016972063023e-05 * YEAR,
			9.54791938424326609e-04 * SOLAR_MASS });
	self->bodies.push_back(new PlainBody{ 8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
			-2.76742510726862411e-03 * YEAR, 4.99852801234917238e-03 * YEAR, 2.30417297573763929e-05 * YEAR,
			2.85885980666130812e-04 * SOLAR_MASS });
	self->bodies.push_back(new PlainBody{ 1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
			2.96460137564761618e-03 * YEAR, 2.37847173959480950e-03 * YEAR, -2.96589568540237556e-05 * YEAR,
			4.36624404335156298e-05 * SOLAR_MASS });
	self->bodies.push_back(new PlainBody{ 1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
			2.68067772490389322e-03 * YEAR, 1.62824170038242295e-03 * YEAR, -9.51592254519715870e-05 * YEAR,
			5.15138902046611451e-05 * SOLAR_MASS });
	double px = 0, py = 0, pz = 0;
	for (PlainBody *b : self->bodies) {
		px += b->vx * b->mass;
		py += b->vy * b->mass;
		pz += b->vz * b->mass;
	}
	self->bodies[0]->vx = -px / SOLAR_MASS;
	self->bodies[0]->vy = -py / SOLAR_MASS;
	self->bodies[0]->vz = -pz / SOLAR_MASS;
}
void FloorNBody__process(FloorNBody *self, double delta) {
	PlainBody **bodies = self->bodies.data();
	const int n = (int)self->bodies.size();
	const double dt = 0.01;
	for (int step = 0; step < 2000; step++) {
		for (int i = 0; i < n; i++) {
			PlainBody *bi = bodies[i];
			for (int j = i + 1; j < n; j++) {
				PlainBody *bj = bodies[j];
				double dx = bi->x - bj->x;
				double dy = bi->y - bj->y;
				double dz = bi->z - bj->z;
				double d2 = dx * dx + dy * dy + dz * dz;
				double mag = dt / (d2 * std::sqrt(d2));
				bi->vx -= dx * bj->mass * mag;
				bi->vy -= dy * bj->mass * mag;
				bi->vz -= dz * bj->mass * mag;
				bj->vx += dx * bi->mass * mag;
				bj->vy += dy * bi->mass * mag;
				bj->vz += dz * bi->mass * mag;
			}
		}
		for (int i = 0; i < n; i++) {
			PlainBody *b = bodies[i];
			b->x += dt * b->vx;
			b->y += dt * b->vy;
			b->z += dt * b->vz;
		}
	}
}
double FloorNBody_checksum(FloorNBody *self) {
	double e = 0.0;
	const int n = (int)self->bodies.size();
	for (int i = 0; i < n; i++) {
		PlainBody *bi = self->bodies[i];
		e += 0.5 * bi->mass * (bi->vx * bi->vx + bi->vy * bi->vy + bi->vz * bi->vz);
		for (int j = i + 1; j < n; j++) {
			PlainBody *bj = self->bodies[j];
			double dx = bi->x - bj->x;
			double dy = bi->y - bj->y;
			double dz = bi->z - bj->z;
			e -= (bi->mass * bj->mass) / std::sqrt(dx * dx + dy * dy + dz * dz);
		}
	}
	return e;
}
AS3_THUNK_VOID(FloorNBody, _ready)
AS3_THUNK_DELTA(FloorNBody, _process)
AS3_THUNK_RET_NUMBER(FloorNBody, checksum)
const as3::MethodEntry nbody_methods[] = {
	{ "_ready", FloorNBody_thunk__ready },
	{ "_process", FloorNBody_thunk__process },
	{ "checksum", FloorNBody_thunk_checksum },
};
const as3::ScriptClass nbody_class = { "FloorNBody", "Node2D", FloorNBody_create, nbody_methods, 3, nullptr, 0 };
const as3::AutoRegister nbody_register(&nbody_class);

// ---- sieve: plain array, no checks ----
struct FloorSieve : as3::Object {
	GDExtensionObjectPtr owner;
	std::vector<int32_t> flags = std::vector<int32_t>(200000);
	int32_t total = 0;
};
AS3_CLASS(FloorSieve)
void FloorSieve__process(FloorSieve *self, double delta) {
	int32_t *flags = self->flags.data();
	const int32_t limit = 200000;
	for (int32_t i = 0; i < limit; i++) {
		flags[i] = 1;
	}
	int32_t count = 0;
	for (int32_t i = 2; i < limit; i++) {
		if (flags[i] == 1) {
			count++;
			for (int32_t j = i + i; j < limit; j += i) {
				flags[j] = 0;
			}
		}
	}
	self->total += count;
}
double FloorSieve_checksum(FloorSieve *self) {
	return (double)self->total;
}
AS3_THUNK_DELTA(FloorSieve, _process)
AS3_THUNK_RET_NUMBER(FloorSieve, checksum)
const as3::MethodEntry sieve_methods[] = {
	{ "_process", FloorSieve_thunk__process },
	{ "checksum", FloorSieve_thunk_checksum },
};
const as3::ScriptClass sieve_class = { "FloorSieve", "Node2D", FloorSieve_create, sieve_methods, 2, nullptr, 0 };
const as3::AutoRegister sieve_register(&sieve_class);

} // namespace
