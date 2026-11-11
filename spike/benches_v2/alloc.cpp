// Hand translation of as3/Alloc.as (holdout) under the version 2 rules.
#include "benches_v2/shape2.h"

namespace {

const int32_t COUNT = 20000;

struct Particle : as3::Object {
	double x, y, vx, vy;
};
void Particle_destroy(as3::Object *p_object) {
	as3::pool_delete(static_cast<Particle *>(p_object));
}
const as3::Class Particle_class = { "Particle", Particle_destroy };

// Alloc.as:36   the Particle constructor, from the size-class pool (rule 14)
inline Particle *Particle_new(double x, double y, double vx, double vy) {
	Particle *self = as3::pool_new<Particle>();
	self->refcount = 0;
	self->cls = &Particle_class;
	self->x = x;
	self->y = y;
	self->vx = vx;
	self->vy = vy;
	return self;
}

typedef as3::Vector<as3::Ref<Particle>> ParticleVector;

struct V2Alloc : as3::Object {
	GDExtensionObjectPtr owner;
	as3::Ref<ParticleVector> slots{ ParticleVector::create(COUNT) };
	int32_t frame = 0;
	double last = 0.0;
};
AS3_CLASS2(V2Alloc)

__attribute__((noinline)) void V2Alloc__process_checked(V2Alloc *self, as3::Ref<Particle> *const data,
		const int32_t length, const int32_t frame) {
	AS3_FN("Alloc/_process", "Alloc.as");
	int32_t i;
	for (i = 0; i < COUNT; i = as3::add_int(i, 1)) {
		AS3_CHECK_BOUNDS_VOID(i, length, 16);
		data[i] = as3::Ref<Particle>(Particle_new((double)i, (double)frame, (double)i * 0.5, (double)frame * 0.25));
	}
	double sum = 0.0;
	for (i = 0; i < COUNT; i = as3::add_int(i, 1)) {
		AS3_CHECK_BOUNDS_VOID(i, length, 20);
		Particle *p = data[i].get();
		AS3_CHECK_NULL_VOID(p, 21);
		sum += p->x + p->vy;
	}
	self->last = sum;
}

void V2Alloc__process(V2Alloc *self, double delta) {
	AS3_FN("Alloc/_process", "Alloc.as");
	// Alloc.as:13   frame++;
	const int32_t frame = as3::add_int(self->frame, 1);
	self->frame = frame;
	// The only calls below are the Particle constructor and, when a slot's old value is released, the
	// Particle destructor. Neither runs user code that could touch `slots`, so its length and data stay in
	// locals (rule 7).
	ParticleVector *slots = self->slots.get();
	AS3_CHECK_NULL_VOID(slots, 16);
	const int32_t length = slots->length;
	as3::Ref<Particle> *const data = slots->data;
	// Loop versioning (rule 7): every index is in [0, COUNT).
	if (AS3_UNLIKELY(COUNT > length)) {
		V2Alloc__process_checked(self, data, length, frame);
		return;
	}
	int32_t i;
	for (i = 0; i < COUNT; i = as3::add_int(i, 1)) {
		// Alloc.as:16   slots[i] = new Particle(i, frame, i * 0.5, frame * 0.25);
		// The new object is owned by the slot; the slot's previous object is released (rule 2).
		data[i] = as3::Ref<Particle>(Particle_new((double)i, (double)frame, (double)i * 0.5, (double)frame * 0.25));
	}
	double sum = 0.0;
	for (i = 0; i < COUNT; i = as3::add_int(i, 1)) {
		// Alloc.as:20   var p:Particle = slots[i];   (borrowed: nothing in its lifetime releases the slot)
		Particle *p = data[i].get();
		// Alloc.as:21   sum += p.x + p.vy;
		AS3_CHECK_NULL_VOID(p, 21);
		sum += p->x + p->vy;
	}
	// Alloc.as:23   last = sum;
	self->last = sum;
}

double V2Alloc_checksum(V2Alloc *self) {
	return self->last;
}

AS3_THUNK_DELTA(V2Alloc, _process)
AS3_THUNK_RET_NUMBER(V2Alloc, checksum)

const as3::MethodEntry methods[] = {
	{ "_process", V2Alloc_thunk__process },
	{ "checksum", V2Alloc_thunk_checksum },
};
const as3::ScriptClass script_class = { "V2Alloc", "Node2D", V2Alloc_create, methods, 2, nullptr, 0 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
