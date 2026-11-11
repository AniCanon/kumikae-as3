// Hand translation of as3/V2Probe.as under the version 2 rules.
#include "benches_v2/shape2.h"

namespace {

struct V2Probe : as3::Object {
	GDExtensionObjectPtr owner;
	int32_t state = 0;
};
AS3_CLASS2(V2Probe)

void V2Probe_on_boom(V2Probe *self) {
	AS3_FN("V2Probe/on_boom", "V2Probe.as");
	// V2Probe.as:11   state++;
	self->state = as3::add_int(self->state, 1);
	// V2Probe.as:12   throw new Error("in handler");
	AS3_THROW_VOID("Error", "in handler", 12);
}
void V2Probe_thunk_on_boom(as3::Object *p_self, const GDExtensionConstVariantPtr *, int64_t, GDExtensionVariantPtr) {
	V2Probe_on_boom(static_cast<V2Probe *>(p_self));
}

void V2Probe_bump(V2Probe *self) {
	// V2Probe.as:19   state += 10;   (cannot throw)
	self->state = as3::add_int(self->state, 10);
}
void V2Probe_thunk_bump(as3::Object *p_self, const GDExtensionConstVariantPtr *, int64_t, GDExtensionVariantPtr) {
	V2Probe_bump(static_cast<V2Probe *>(p_self));
}

void V2Probe__ready(V2Probe *self) {
	// V2Probe.as:8   connect("boom", on_boom);   (a method closure is a native callable: rule 10)
	as3::connect_closure(self->owner, "boom", self, V2Probe_thunk_on_boom);
}

void V2Probe_fire(V2Probe *self) {
	// V2Probe.as:9   emit_signal("boom"); state++;
	static const godot::Variant signal_name = godot::StringName("boom");
	const godot::Variant *args[] = { &signal_name };
	as3::varcall(binds::Object_emit_signal, self->owner, args, 1);
	// No error test: an engine call never returns with an AS3 error pending (rule 4).
	self->state = as3::add_int(self->state, 1);
}

void V2Probe_leaf(V2Probe *self, as3::Object *o) {
	AS3_FN("V2Probe/leaf", "V2Probe.as");
	// V2Probe.as:16   o.toString();
	AS3_CHECK_NULL_VOID(o, 16);
}

void V2Probe_middle(V2Probe *self) {
	AS3_FN("V2Probe/middle", "V2Probe.as");
	// V2Probe.as:15   leaf(null);   (leaf can throw: test and propagate)
	V2Probe_leaf(self, nullptr);
	AS3_PROPAGATE_VOID(15);
}

void V2Probe_chain(V2Probe *self) {
	AS3_FN("V2Probe/chain", "V2Probe.as");
	// V2Probe.as:14   middle();
	V2Probe_middle(self);
	AS3_PROPAGATE_VOID(14);
}

AS3_THUNK_VOID(V2Probe, _ready)
AS3_THUNK_VOID(V2Probe, fire)
AS3_THUNK_VOID(V2Probe, chain)
void V2Probe_thunk_get_state(as3::Object *p_self, const GDExtensionConstVariantPtr *, int64_t,
		GDExtensionVariantPtr r_ret) {
	as3::ret_int(r_ret, static_cast<V2Probe *>(p_self)->state);
}
void V2Probe_thunk_get_closure(as3::Object *p_self, const GDExtensionConstVariantPtr *, int64_t,
		GDExtensionVariantPtr r_ret) {
	V2Probe *self = static_cast<V2Probe *>(p_self);
	// V2Probe.as:18   return bump;
	*reinterpret_cast<godot::Variant *>(r_ret) = as3::make_closure(self->owner, self, V2Probe_thunk_bump);
}

const as3::MethodEntry methods[] = {
	{ "_ready", V2Probe_thunk__ready },
	{ "fire", V2Probe_thunk_fire },
	{ "chain", V2Probe_thunk_chain },
	{ "on_boom", V2Probe_thunk_on_boom },
	{ "bump", V2Probe_thunk_bump },
	{ "get_state", V2Probe_thunk_get_state },
	{ "get_closure", V2Probe_thunk_get_closure },
};
const char *const signals[] = { "boom" };
const as3::ScriptClass script_class = { "V2Probe", "Node", V2Probe_create, methods, 7, signals, 1 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
