// Hand translation of as3/SignalTraffic.as under the version 2 rules.
#include "benches_v2/shape2.h"

namespace {

const int32_t EMITS = 20000;

struct V2SignalTraffic : as3::Object {
	GDExtensionObjectPtr owner;
	double total = 0.0;
};
AS3_CLASS2(V2SignalTraffic)

void V2SignalTraffic_on_hit(V2SignalTraffic *self, int32_t amount, godot::Vector2 pos) {
	// SignalTraffic.as:22   total += amount + pos.x;   (cannot throw)
	self->total += (double)amount + (double)pos.x;
}
void V2SignalTraffic_thunk_on_hit(as3::Object *p_self, const GDExtensionConstVariantPtr *p_args, int64_t,
		GDExtensionVariantPtr) {
	V2SignalTraffic_on_hit(static_cast<V2SignalTraffic *>(p_self), as3::arg_int(p_args[0]),
			as3::arg_vector2(p_args[1]));
}

void V2SignalTraffic__ready(V2SignalTraffic *self) {
	// SignalTraffic.as:12   connect("hit", on_hit);   (a method closure is a native callable: rule 10)
	as3::connect_closure(self->owner, "hit", self, V2SignalTraffic_thunk_on_hit);
}

void V2SignalTraffic__process(V2SignalTraffic *self, double delta) {
	static const godot::Variant hit_name = godot::StringName("hit");
	for (int32_t i = 0; i < EMITS; i = as3::add_int(i, 1)) {
		// SignalTraffic.as:17   emit_signal("hit", i, new Vector2(i, 1));
		// A vararg engine method: the boxed call is the only path. No error test after it (rule 4).
		godot::Variant amount((int64_t)i);
		godot::Variant pos(godot::Vector2((float)i, 1.0f));
		const godot::Variant *args[] = { &hit_name, &amount, &pos };
		as3::varcall(binds::Object_emit_signal, self->owner, args, 3);
	}
}

double V2SignalTraffic_checksum(V2SignalTraffic *self) {
	return self->total;
}

AS3_THUNK_VOID(V2SignalTraffic, _ready)
AS3_THUNK_DELTA(V2SignalTraffic, _process)
AS3_THUNK_RET_NUMBER(V2SignalTraffic, checksum)

const as3::MethodEntry methods[] = {
	{ "_ready", V2SignalTraffic_thunk__ready },
	{ "_process", V2SignalTraffic_thunk__process },
	{ "on_hit", V2SignalTraffic_thunk_on_hit },
	{ "checksum", V2SignalTraffic_thunk_checksum },
};
const char *const signals[] = { "hit" };
const as3::ScriptClass script_class = { "V2SignalTraffic", "Node2D", V2SignalTraffic_create, methods, 4, signals, 1 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
