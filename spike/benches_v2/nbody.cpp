// Hand translation of as3/NBody.as under the version 2 rules.
#include "benches_v2/shape2.h"

namespace {

const int32_t STEPS = 2000;
const double PI = 3.141592653589793;
const double SOLAR_MASS = 4 * PI * PI;
const double YEAR = 365.24;

struct Body2 : as3::Object {
	double x, y, z, vx, vy, vz, mass;
};
void Body2_destroy(as3::Object *p_object) {
	as3::pool_delete(static_cast<Body2 *>(p_object));
}
const as3::Class Body2_class = { "Body", Body2_destroy };

Body2 *Body2_new(double x, double y, double z, double vx, double vy, double vz, double mass) {
	Body2 *self = as3::pool_new<Body2>();
	self->refcount = 0;
	self->cls = &Body2_class;
	self->x = x;
	self->y = y;
	self->z = z;
	self->vx = vx;
	self->vy = vy;
	self->vz = vz;
	self->mass = mass;
	return self;
}

typedef as3::Vector<as3::Ref<Body2>> BodyVector;

struct V2NBody : as3::Object {
	GDExtensionObjectPtr owner;
	as3::Ref<BodyVector> bodies{ BodyVector::create() };
};
AS3_CLASS2(V2NBody)

void V2NBody__ready(V2NBody *self) {
	AS3_FN("NBody/_ready", "NBody.as");
	BodyVector *bodies = self->bodies.get();
	AS3_CHECK_NULL_VOID(bodies, 12);
	bodies->push(as3::Ref<Body2>(Body2_new(0, 0, 0, 0, 0, 0, SOLAR_MASS)));
	bodies->push(as3::Ref<Body2>(Body2_new(4.84143144246472090e+00, -1.16032004402742839e+00,
			-1.03622044471123109e-01, 1.66007664274403694e-03 * YEAR, 7.69901118419740425e-03 * YEAR,
			-6.90460016972063023e-05 * YEAR, 9.54791938424326609e-04 * SOLAR_MASS)));
	bodies->push(as3::Ref<Body2>(Body2_new(8.34336671824457987e+00, 4.12479856412430479e+00,
			-4.03523417114321381e-01, -2.76742510726862411e-03 * YEAR, 4.99852801234917238e-03 * YEAR,
			2.30417297573763929e-05 * YEAR, 2.85885980666130812e-04 * SOLAR_MASS)));
	bodies->push(as3::Ref<Body2>(Body2_new(1.28943695621391310e+01, -1.51111514016986312e+01,
			-2.23307578892655734e-01, 2.96460137564761618e-03 * YEAR, 2.37847173959480950e-03 * YEAR,
			-2.96589568540237556e-05 * YEAR, 4.36624404335156298e-05 * SOLAR_MASS)));
	bodies->push(as3::Ref<Body2>(Body2_new(1.53796971148509165e+01, -2.59193146099879641e+01,
			1.79258772950371181e-01, 2.68067772490389322e-03 * YEAR, 1.62824170038242295e-03 * YEAR,
			-9.51592254519715870e-05 * YEAR, 5.15138902046611451e-05 * SOLAR_MASS)));
	double px = 0.0, py = 0.0, pz = 0.0;
	const int32_t length = bodies->length;
	as3::Ref<Body2> *const data = bodies->data;
	for (int32_t i = 0; i < length; i = as3::add_int(i, 1)) {
		Body2 *b = data[i].get(); // bounds check elided: i < length (rule 7); borrowed (rule 2)
		AS3_CHECK_NULL_VOID(b, 28);
		px += b->vx * b->mass;
		py += b->vy * b->mass;
		pz += b->vz * b->mass;
	}
	AS3_CHECK_BOUNDS_VOID(0, length, 32);
	Body2 *sun = data[0].get();
	AS3_CHECK_NULL_VOID(sun, 33);
	sun->vx = -px / SOLAR_MASS;
	sun->vy = -py / SOLAR_MASS;
	sun->vz = -pz / SOLAR_MASS;
}

void V2NBody_advance(V2NBody *self, double dt) {
	AS3_FN("NBody/advance", "NBody.as");
	// NBody.as:45   var n:int = bodies.length;
	// This function makes no calls, so the vector's length and data live in locals for all of it (rule 7).
	BodyVector *bodies = self->bodies.get();
	AS3_CHECK_NULL_VOID(bodies, 45);
	const int32_t n = bodies->length;
	as3::Ref<Body2> *const data = bodies->data;
	for (int32_t i = 0; i < n; i = as3::add_int(i, 1)) {
		// NBody.as:47   var bi:Body = bodies[i];
		// Bounds check elided: 0 <= i < n and n is the length (rule 7). The local borrows: nothing in its
		// lifetime can release the vector's reference (rule 2).
		Body2 *bi = data[i].get();
		for (int32_t j = as3::add_int(i, 1); j < n; j = as3::add_int(j, 1)) {
			// NBody.as:49   var bj:Body = bodies[j];   (same reasoning: j starts above zero and stays below n)
			Body2 *bj = data[j].get();
			// NBody.as:50   var dx:Number = bi.x - bj.x;   (each local is null-checked at its first use)
			AS3_CHECK_NULL_VOID(bi, 50);
			AS3_CHECK_NULL_VOID(bj, 50);
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
	for (int32_t i = 0; i < n; i = as3::add_int(i, 1)) {
		// NBody.as:64   var b:Body = bodies[i];
		Body2 *b = data[i].get();
		// NBody.as:65   b.x += dt * b.vx;
		AS3_CHECK_NULL_VOID(b, 65);
		b->x += dt * b->vx;
		b->y += dt * b->vy;
		b->z += dt * b->vz;
	}
}

void V2NBody__process(V2NBody *self, double delta) {
	AS3_FN("NBody/_process", "NBody.as");
	for (int32_t step = 0; step < STEPS; step = as3::add_int(step, 1)) {
		// NBody.as:40   advance(0.01);   (advance can throw: test and propagate, rule 4)
		V2NBody_advance(self, 0.01);
		AS3_PROPAGATE_VOID(40);
	}
}

double V2NBody_checksum(V2NBody *self) {
	AS3_FN("NBody/checksum", "NBody.as");
	double e = 0.0;
	BodyVector *bodies = self->bodies.get();
	AS3_CHECK_NULL(bodies, 73);
	const int32_t n = bodies->length;
	as3::Ref<Body2> *const data = bodies->data;
	for (int32_t i = 0; i < n; i = as3::add_int(i, 1)) {
		Body2 *bi = data[i].get();
		AS3_CHECK_NULL(bi, 76);
		e += 0.5 * bi->mass * (bi->vx * bi->vx + bi->vy * bi->vy + bi->vz * bi->vz);
		for (int32_t j = as3::add_int(i, 1); j < n; j = as3::add_int(j, 1)) {
			Body2 *bj = data[j].get();
			AS3_CHECK_NULL(bj, 79);
			double dx = bi->x - bj->x;
			double dy = bi->y - bj->y;
			double dz = bi->z - bj->z;
			e -= (bi->mass * bj->mass) / std::sqrt(dx * dx + dy * dy + dz * dz);
		}
	}
	return e;
}

AS3_THUNK_VOID(V2NBody, _ready)
AS3_THUNK_DELTA(V2NBody, _process)
AS3_THUNK_RET_NUMBER(V2NBody, checksum)

const as3::MethodEntry methods[] = {
	{ "_ready", V2NBody_thunk__ready },
	{ "_process", V2NBody_thunk__process },
	{ "checksum", V2NBody_thunk_checksum },
};
const as3::ScriptClass script_class = { "V2NBody", "Node2D", V2NBody_create, methods, 3, nullptr, 0 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
