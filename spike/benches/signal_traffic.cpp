// Hand translation of as3/SignalTraffic.as.
#include "benches/shape.h"

namespace {

const int32_t EMITS = 20000;

struct SignalTraffic : as3::Object {
	GDExtensionObjectPtr owner;
	double total = 0.0;
};
AS3_CLASS(SignalTraffic)

void SignalTraffic__ready(SignalTraffic *self) {
	AS3_FRAME("SignalTraffic/_ready", "SignalTraffic.as");
	// SignalTraffic.as:12   connect("hit", on_hit);
	AS3_LINE(12);
	as3::connect_method(self->owner, "hit", "on_hit");
	if (AS3_PENDING()) {
		return;
	}
}

void SignalTraffic__process(SignalTraffic *self, double delta) {
	AS3_FRAME("SignalTraffic/_process", "SignalTraffic.as");
	// The signal's name is a constant the compiler would intern once.
	static const godot::Variant hit_name = godot::StringName("hit");
	for (int32_t i = 0; i < EMITS; i = as3::add_int(i, 1)) {
		// SignalTraffic.as:17   emit_signal("hit", i, new Vector2(i, 1));
		// emit_signal takes a variable number of arguments, so the engine only offers the boxed path.
		AS3_LINE(17);
		godot::Variant amount((int64_t)i);
		godot::Variant pos(godot::Vector2((float)i, 1.0f));
		const godot::Variant *args[] = { &hit_name, &amount, &pos };
		as3::varcall(binds::Object_emit_signal, self->owner, args, 3);
		if (AS3_PENDING()) {
			return;
		}
	}
}

void SignalTraffic_on_hit(SignalTraffic *self, int32_t amount, godot::Vector2 pos) {
	// SignalTraffic.as:22   total += amount + pos.x;   (cannot throw)
	self->total += (double)amount + (double)pos.x;
}

double SignalTraffic_checksum(SignalTraffic *self) {
	// SignalTraffic.as:26   return total;
	return self->total;
}

AS3_THUNK_VOID(SignalTraffic, _ready)
AS3_THUNK_DELTA(SignalTraffic, _process)
AS3_THUNK_RET_NUMBER(SignalTraffic, checksum)
void SignalTraffic_thunk_on_hit(as3::Object *p_self, const GDExtensionConstVariantPtr *p_args, int64_t,
		GDExtensionVariantPtr) {
	SignalTraffic_on_hit(static_cast<SignalTraffic *>(p_self), as3::arg_int(p_args[0]), as3::arg_vector2(p_args[1]));
}

const as3::MethodEntry methods[] = {
	{ "_ready", SignalTraffic_thunk__ready },
	{ "_process", SignalTraffic_thunk__process },
	{ "on_hit", SignalTraffic_thunk_on_hit },
	{ "checksum", SignalTraffic_thunk_checksum },
};
const char *const signals[] = { "hit" };
const as3::ScriptClass script_class = { "SignalTraffic", "Node2D", SignalTraffic_create, methods, 4, signals, 1 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
