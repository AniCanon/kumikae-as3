// Hand translation of as3/Probe.as, following the plan's translation rules.
#include "benches/binds.h"
#include "host/class_registry.h"
#include "runtime/godot_calls.h"

namespace {

struct Probe : as3::Object {
	GDExtensionObjectPtr owner;
	int32_t count;
};

void Probe_destroy(as3::Object *p_object) {
	delete static_cast<Probe *>(p_object);
}
const as3::Class Probe_class = { "Probe", Probe_destroy };

as3::Object *Probe_create(GDExtensionObjectPtr p_owner) {
	Probe *self = new Probe();
	self->refcount = 0;
	self->cls = &Probe_class;
	self->owner = p_owner;
	self->count = 0;
	return self;
}

void Probe__ready(Probe *self) {
	AS3_FRAME("Probe/_ready", "Probe.as");
	// Probe.as:8   count = 0; connect("pinged", on_pinged);
	self->count = 0;
	AS3_LINE(8);
	as3::connect_method(self->owner, "pinged", "on_pinged");
	if (AS3_PENDING()) {
		return;
	}
}

void Probe__process(Probe *self, double delta) {
	// Probe.as:9   count++;   (cannot throw: no frame needed for a leaf with no throwing operation)
	self->count = as3::add_int(self->count, 1);
}

int32_t Probe_get_count(Probe *self) {
	// Probe.as:10   return count;
	return self->count;
}

void Probe_explode(Probe *self) {
	AS3_FRAME("Probe/explode", "Probe.as");
	// Probe.as:12   throw new Error("boom");
	AS3_LINE(12);
	as3::throw_error("Error", "boom");
	return;
}

void Probe_ping(Probe *self) {
	AS3_FRAME("Probe/ping", "Probe.as");
	// Probe.as:14   emit_signal("pinged");
	AS3_LINE(14);
	static const godot::Variant signal_name = godot::StringName("pinged");
	const godot::Variant *args[] = { &signal_name };
	as3::varcall(binds::Object_emit_signal, self->owner, args, 1);
	if (AS3_PENDING()) {
		return;
	}
}

void Probe_on_pinged(Probe *self) {
	// Probe.as:15   count += 100;
	self->count = as3::add_int(self->count, 100);
}

void thunk__ready(as3::Object *p_self, const GDExtensionConstVariantPtr *, int64_t, GDExtensionVariantPtr) {
	Probe__ready(static_cast<Probe *>(p_self));
}
void thunk__process(as3::Object *p_self, const GDExtensionConstVariantPtr *p_args, int64_t, GDExtensionVariantPtr) {
	Probe__process(static_cast<Probe *>(p_self), as3::arg_number(p_args[0]));
}
void thunk_get_count(as3::Object *p_self, const GDExtensionConstVariantPtr *, int64_t, GDExtensionVariantPtr r_ret) {
	as3::ret_int(r_ret, Probe_get_count(static_cast<Probe *>(p_self)));
}
void thunk_explode(as3::Object *p_self, const GDExtensionConstVariantPtr *, int64_t, GDExtensionVariantPtr) {
	Probe_explode(static_cast<Probe *>(p_self));
}
void thunk_ping(as3::Object *p_self, const GDExtensionConstVariantPtr *, int64_t, GDExtensionVariantPtr) {
	Probe_ping(static_cast<Probe *>(p_self));
}
void thunk_on_pinged(as3::Object *p_self, const GDExtensionConstVariantPtr *, int64_t, GDExtensionVariantPtr) {
	Probe_on_pinged(static_cast<Probe *>(p_self));
}

const as3::MethodEntry methods[] = {
	{ "_ready", thunk__ready },
	{ "_process", thunk__process },
	{ "get_count", thunk_get_count },
	{ "explode", thunk_explode },
	{ "ping", thunk_ping },
	{ "on_pinged", thunk_on_pinged },
};
const char *const signals[] = { "pinged" };
const as3::ScriptClass script_class = { "Probe", "Node", Probe_create, methods, 6, signals, 1 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
