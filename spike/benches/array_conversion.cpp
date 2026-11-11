// Hand translation of as3/ArrayConversion.as.
#include "benches/shape.h"

namespace {

const int32_t ROUNDS = 20;
const int32_t SIZE = 1000;

struct ArrayConversion : as3::Object {
	GDExtensionObjectPtr owner;
	GDExtensionObjectPtr line = nullptr;
};
AS3_CLASS(ArrayConversion)

void ArrayConversion__ready(ArrayConversion *self) {
	AS3_FRAME("ArrayConversion/_ready", "ArrayConversion.as");
	// ArrayConversion.as:13   line = new Line2D();
	AS3_LINE(13);
	self->line = as3::construct_object("Line2D");
	// ArrayConversion.as:14   add_child(line);
	AS3_LINE(14);
	as3::add_child(self->owner, self->line);
	if (AS3_PENDING()) {
		return;
	}
}

void ArrayConversion__process(ArrayConversion *self, double delta) {
	AS3_FRAME("ArrayConversion/_process", "ArrayConversion.as");
	static const godot::StringName meta_name("data");
	for (int32_t k = 0; k < ROUNDS; k = as3::add_int(k, 1)) {
		// ArrayConversion.as:19   var points:Vector.<Vector2> = new Vector.<Vector2>();
		as3::Ref<as3::Vector<godot::Vector2>> points(as3::Vector<godot::Vector2>::create());
		int32_t i;
		for (i = 0; i < SIZE; i = as3::add_int(i, 1)) {
			// ArrayConversion.as:21   points.push(new Vector2(i, k));   (points was constructed in this block)
			points->push(godot::Vector2((float)i, (float)k));
		}
		// ArrayConversion.as:23   line.points = points;   (the AS3 vector is copied to a Godot packed array)
		AS3_LINE(23);
		AS3_NULLCHECK_VOID(self->line);
		godot::PackedVector2Array packed = as3::to_packed_vector2(points.get());
		as3::ptrcall_void(binds::Line2D_set_points, self->line, &packed);
		if (AS3_PENDING()) {
			return;
		}
		// ArrayConversion.as:24   var data:Array = [];
		as3::Ref<as3::Array> data(as3::Array::create());
		for (i = 0; i < SIZE; i = as3::add_int(i, 1)) {
			// ArrayConversion.as:26   data.push(i + k);
			data->push(as3::Value(as3::add_int(i, k)));
		}
		// ArrayConversion.as:28   set_meta("data", data);   (the AS3 array is copied to a Godot array)
		AS3_LINE(28);
		godot::Variant boxed = as3::to_godot_array(data.get());
		as3::ptrcall_void(binds::Object_set_meta, self->owner, &meta_name, &boxed);
		if (AS3_PENDING()) {
			return;
		}
	}
}

double ArrayConversion_checksum(ArrayConversion *self) {
	AS3_FRAME("ArrayConversion/checksum", "ArrayConversion.as");
	static const godot::StringName meta_name("data");
	// ArrayConversion.as:33   var data:Array = get_meta("data") as Array;
	AS3_LINE(33);
	godot::Variant fallback;
	godot::Variant meta = as3::ptrcall_ret<godot::Variant>(binds::Object_get_meta, self->owner, &meta_name, &fallback);
	if (AS3_PENDING()) {
		return 0.0;
	}
	as3::Ref<as3::Array> data;
	if (meta.get_type() == godot::Variant::ARRAY) {
		data = as3::from_godot_array(meta);
	}
	// ArrayConversion.as:34   return line.get_point_count() + data.length;
	AS3_LINE(34);
	AS3_NULLCHECK(self->line);
	int64_t points = as3::ptrcall_ret<int64_t>(binds::Line2D_get_point_count, self->line);
	if (AS3_PENDING()) {
		return 0.0;
	}
	AS3_NULLCHECK(data.get());
	return (double)points + (double)data->length();
}

AS3_THUNK_VOID(ArrayConversion, _ready)
AS3_THUNK_DELTA(ArrayConversion, _process)
AS3_THUNK_RET_NUMBER(ArrayConversion, checksum)

const as3::MethodEntry methods[] = {
	{ "_ready", ArrayConversion_thunk__ready },
	{ "_process", ArrayConversion_thunk__process },
	{ "checksum", ArrayConversion_thunk_checksum },
};
const as3::ScriptClass script_class = { "ArrayConversion", "Node2D", ArrayConversion_create, methods, 3, nullptr, 0 };
const as3::AutoRegister auto_register(&script_class);

} // namespace
