#include "runtime/godot_calls.h"

#include "host/spike_script.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

using namespace godot;

namespace as3 {

GDExtensionTypeFromVariantConstructorFunc variant_to_float = nullptr;
GDExtensionTypeFromVariantConstructorFunc variant_to_int = nullptr;
GDExtensionTypeFromVariantConstructorFunc variant_to_vector2 = nullptr;
GDExtensionVariantFromTypeConstructorFunc variant_from_float = nullptr;
GDExtensionVariantFromTypeConstructorFunc variant_from_int = nullptr;

static GDExtensionMethodBindPtr refcounted_init_ref = nullptr;
static GDExtensionMethodBindPtr refcounted_reference = nullptr;
static GDExtensionMethodBindPtr refcounted_unreference = nullptr;

void godot_calls_init() {
	using namespace gdextension_interface;
	variant_to_float = get_variant_to_type_constructor(GDEXTENSION_VARIANT_TYPE_FLOAT);
	variant_to_int = get_variant_to_type_constructor(GDEXTENSION_VARIANT_TYPE_INT);
	variant_to_vector2 = get_variant_to_type_constructor(GDEXTENSION_VARIANT_TYPE_VECTOR2);
	variant_from_float = get_variant_from_type_constructor(GDEXTENSION_VARIANT_TYPE_FLOAT);
	variant_from_int = get_variant_from_type_constructor(GDEXTENSION_VARIANT_TYPE_INT);
	refcounted_init_ref = bind("RefCounted", "init_ref", 2240911060);
	refcounted_reference = bind("RefCounted", "reference", 2240911060);
	refcounted_unreference = bind("RefCounted", "unreference", 2240911060);
}

GDExtensionMethodBindPtr bind(const char *p_class, const char *p_method, int64_t p_hash) {
	StringName class_name(p_class);
	StringName method_name(p_method);
	GDExtensionMethodBindPtr result = gdextension_interface::classdb_get_method_bind(class_name._native_ptr(),
			method_name._native_ptr(), p_hash);
	if (!result) {
		std::fprintf(stderr, "kas3_spike: no method bind for %s.%s (hash %lld)\n", p_class, p_method,
				(long long)p_hash);
		std::abort();
	}
	return result;
}

void varcall(GDExtensionMethodBindPtr p_bind, GDExtensionObjectPtr p_object, const Variant *const *p_args,
		int64_t p_argc) {
	Variant ret;
	GDExtensionCallError error;
	gdextension_interface::object_method_bind_call(p_bind, p_object,
			reinterpret_cast<const GDExtensionConstVariantPtr *>(p_args), p_argc, ret._native_ptr(), &error);
}

GDExtensionObjectPtr construct_object(const char *p_class) {
	StringName class_name(p_class);
	return gdextension_interface::classdb_construct_object2(class_name._native_ptr());
}

GodotRef GodotRef::construct(const char *p_class) {
	GDExtensionObjectPtr object = construct_object(p_class);
	ptrcall_ret<bool>(refcounted_init_ref, object);
	return adopt(object);
}

GodotRef::GodotRef(const GodotRef &p_other) :
		ptr(p_other.ptr) {
	if (ptr) {
		ptrcall_ret<bool>(refcounted_reference, ptr);
	}
}

GodotRef &GodotRef::operator=(const GodotRef &p_other) {
	if (p_other.ptr) {
		ptrcall_ret<bool>(refcounted_reference, p_other.ptr);
	}
	drop();
	ptr = p_other.ptr;
	return *this;
}

void GodotRef::drop() {
	if (ptr && ptrcall_ret<bool>(refcounted_unreference, ptr)) {
		gdextension_interface::object_destroy(ptr);
	}
	ptr = nullptr;
}

static void destroy_boxed(Object *p_object) {
	delete static_cast<BoxedVariant *>(p_object);
}

const Class *BoxedVariant::class_info() {
	static const Class info = { "BoxedVariant", destroy_boxed };
	return &info;
}

BoxedVariant *BoxedVariant::create(const Variant &p_value) {
	BoxedVariant *boxed = new BoxedVariant();
	boxed->refcount = 0;
	boxed->cls = class_info();
	boxed->value = p_value;
	return boxed;
}

Variant to_variant(const Value &p_value) {
	switch (p_value.tag) {
		case Value::INT:
			return Variant((int64_t)p_value.i);
		case Value::NUMBER:
			return Variant(p_value.n);
		case Value::BOOLEAN:
			return Variant(p_value.b);
		case Value::OBJECT:
			if (p_value.o->cls == BoxedVariant::class_info()) {
				return static_cast<BoxedVariant *>(p_value.o)->value;
			}
			return Variant();
		default:
			return Variant();
	}
}

Value from_variant(const Variant &p_variant) {
	switch (p_variant.get_type()) {
		case Variant::NIL:
			return Value((Object *)nullptr);
		case Variant::BOOL:
			return Value((bool)p_variant);
		case Variant::INT: {
			int64_t wide = p_variant;
			if (wide >= INT32_MIN && wide <= INT32_MAX) {
				return Value((int32_t)wide);
			}
			return Value((double)wide);
		}
		case Variant::FLOAT:
			return Value((double)p_variant);
		default:
			return Value(BoxedVariant::create(p_variant));
	}
}

bool value_to_vector2(const Value &p_value, Vector2 *r_out) {
	if (p_value.tag != Value::OBJECT || p_value.o->cls != BoxedVariant::class_info()) {
		return false;
	}
	const Variant &boxed = static_cast<BoxedVariant *>(p_value.o)->value;
	if (boxed.get_type() != Variant::VECTOR2) {
		return false;
	}
	*r_out = boxed;
	return true;
}

godot::Array to_godot_array(const Array *p_array) {
	godot::Array result;
	int32_t length = p_array->length();
	result.resize(length);
	for (int32_t i = 0; i < length; i++) {
		result[i] = to_variant(p_array->items[(size_t)i]);
	}
	return result;
}

PackedVector2Array to_packed_vector2(const Vector<Vector2> *p_vector) {
	PackedVector2Array result;
	result.resize(p_vector->length);
	Vector2 *out = result.ptrw();
	for (int32_t i = 0; i < p_vector->length; i++) {
		out[i] = p_vector->data[i];
	}
	return result;
}

Ref<Object_> from_godot_dictionary(const Dictionary &p_dictionary) {
	Ref<Object_> result(Object_::create());
	godot::Array keys = p_dictionary.keys();
	int64_t count = keys.size();
	for (int64_t i = 0; i < count; i++) {
		Variant key = keys[i];
		result->set(String(key), from_variant(p_dictionary[key]));
	}
	return result;
}

Ref<Array> from_godot_array(const godot::Array &p_array) {
	Ref<Array> result(Array::create());
	int64_t count = p_array.size();
	for (int64_t i = 0; i < count; i++) {
		result->push(from_variant(p_array[i]));
	}
	return result;
}

static godot::Object *wrapper_of(GDExtensionObjectPtr p_object) {
	return godot::internal::get_object_instance_binding(p_object);
}

void connect_method(GDExtensionObjectPtr p_owner, const char *p_signal, const char *p_method) {
	godot::Object *owner = wrapper_of(p_owner);
	owner->connect(StringName(p_signal), Callable(owner, StringName(p_method)));
}

void add_child(GDExtensionObjectPtr p_parent, GDExtensionObjectPtr p_child) {
	godot::Object::cast_to<Node>(wrapper_of(p_parent))->add_child(godot::Object::cast_to<Node>(wrapper_of(p_child)));
}

void attach_script(GDExtensionObjectPtr p_object, const char *p_class) {
	wrapper_of(p_object)->set_script(SpikeScript::for_class(StringName(p_class)));
}

void setup_set(GDExtensionObjectPtr p_object, const char *p_property, const Variant &p_value) {
	wrapper_of(p_object)->set(StringName(p_property), p_value);
}

Variant object_variant(GDExtensionObjectPtr p_object) {
	return Variant(wrapper_of(p_object));
}

// ---- method closures as native callables ----

struct Closure {
	Object *self;
	ClosureThunk thunk;
	GDExtensionObjectPtr owner;
};
static std::unordered_map<GDExtensionObjectPtr, std::vector<Closure *>> closures_by_owner;

static void closure_call(void *p_userdata, const GDExtensionConstVariantPtr *p_args, GDExtensionInt p_argc,
		GDExtensionVariantPtr r_return, GDExtensionCallError *r_error) {
	Closure *closure = static_cast<Closure *>(p_userdata);
	if (AS3_UNLIKELY(closure->self == nullptr)) {
		r_error->error = GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL;
		return;
	}
	closure->thunk(closure->self, p_args, p_argc, r_return);
	// Every entry from the engine reports and clears, so no error returns into engine code (rule 4).
	if (AS3_PENDING()) {
		report_uncaught();
	}
	r_error->error = GDEXTENSION_CALL_OK;
}

static GDExtensionBool closure_is_valid(void *p_userdata) {
	return static_cast<Closure *>(p_userdata)->self != nullptr;
}

static void closure_free(void *p_userdata) {
	Closure *closure = static_cast<Closure *>(p_userdata);
	if (closure->self) {
		auto found = closures_by_owner.find(closure->owner);
		if (found != closures_by_owner.end()) {
			std::vector<Closure *> &list = found->second;
			for (size_t i = 0; i < list.size(); i++) {
				if (list[i] == closure) {
					list[i] = list.back();
					list.pop_back();
					break;
				}
			}
		}
	}
	delete closure;
}

Callable make_closure(GDExtensionObjectPtr p_owner, Object *p_self, ClosureThunk p_thunk) {
	Closure *closure = new Closure{ p_self, p_thunk, p_owner };
	closures_by_owner[p_owner].push_back(closure);
	GDExtensionCallableCustomInfo2 info = {};
	info.callable_userdata = closure;
	info.token = gdextension_interface::token;
	info.object_id = gdextension_interface::object_get_instance_id(p_owner);
	info.call_func = closure_call;
	info.is_valid_func = closure_is_valid;
	info.free_func = closure_free;
	Callable callable;
	gdextension_interface::callable_custom_create2(callable._native_ptr(), &info);
	return callable;
}

void connect_closure(GDExtensionObjectPtr p_owner, const char *p_signal, Object *p_self, ClosureThunk p_thunk) {
	wrapper_of(p_owner)->connect(StringName(p_signal), make_closure(p_owner, p_self, p_thunk));
}

void invalidate_closures(GDExtensionObjectPtr p_owner) {
	auto found = closures_by_owner.find(p_owner);
	if (found == closures_by_owner.end()) {
		return;
	}
	for (Closure *closure : found->second) {
		closure->self = nullptr;
	}
	closures_by_owner.erase(found);
}

void report_uncaught() {
	if (!pending_error) {
		return;
	}
	String text = String(pending_error->type) + String(": ") + String(pending_error->message) + String("\n") +
			String(pending_trace());
	UtilityFunctions::printerr(text);
	clear_error();
}

} // namespace as3
