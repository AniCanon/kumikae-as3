// How AS3-shaped code calls the engine: cached method binds and direct typed calls.
// godot-cpp's variant types (String, Array, Variant) are used; its class wrappers are not.
#pragma once

#include "runtime/as3_collections.h"
#include "runtime/as3rt.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace as3 {

// Fetches a method bind once, at load time. Aborts loudly if the engine does not have it.
GDExtensionMethodBindPtr bind(const char *p_class, const char *p_method, int64_t p_hash);

// Direct typed call returning a value. Arguments are pointers to the typed values.
template <class R, class... Args>
inline R ptrcall_ret(GDExtensionMethodBindPtr p_bind, GDExtensionObjectPtr p_object, const Args *...p_args) {
	R ret{};
	const GDExtensionConstTypePtr args[] = { p_args..., nullptr };
	godot::gdextension_interface::object_method_bind_ptrcall(p_bind, p_object, args, &ret);
	return ret;
}

template <class... Args>
inline void ptrcall_void(GDExtensionMethodBindPtr p_bind, GDExtensionObjectPtr p_object, const Args *...p_args) {
	const GDExtensionConstTypePtr args[] = { p_args..., nullptr };
	godot::gdextension_interface::object_method_bind_ptrcall(p_bind, p_object, args, nullptr);
}

// Boxed call for vararg engine methods (emit_signal). The only path those methods have.
void varcall(GDExtensionMethodBindPtr p_bind, GDExtensionObjectPtr p_object, const godot::Variant *const *p_args,
		int64_t p_argc);

// Unboxing of the arguments the engine passes to a script method.
extern GDExtensionTypeFromVariantConstructorFunc variant_to_float;
extern GDExtensionTypeFromVariantConstructorFunc variant_to_int;
extern GDExtensionTypeFromVariantConstructorFunc variant_to_vector2;
extern GDExtensionVariantFromTypeConstructorFunc variant_from_float;
extern GDExtensionVariantFromTypeConstructorFunc variant_from_int;

inline double arg_number(GDExtensionConstVariantPtr p_arg) {
	double value;
	variant_to_float(&value, const_cast<GDExtensionVariantPtr>(p_arg));
	return value;
}
inline int32_t arg_int(GDExtensionConstVariantPtr p_arg) {
	int64_t value;
	variant_to_int(&value, const_cast<GDExtensionVariantPtr>(p_arg));
	return (int32_t)value;
}
inline godot::Vector2 arg_vector2(GDExtensionConstVariantPtr p_arg) {
	godot::Vector2 value;
	variant_to_vector2(&value, const_cast<GDExtensionVariantPtr>(p_arg));
	return value;
}
inline void ret_number(GDExtensionVariantPtr r_ret, double p_value) {
	variant_from_float(r_ret, &p_value);
}
inline void ret_int(GDExtensionVariantPtr r_ret, int32_t p_value) {
	int64_t wide = p_value;
	variant_from_int(r_ret, &wide);
}

// A reference to a Godot RefCounted object: counts through the engine, frees at zero.
class GodotRef {
	GDExtensionObjectPtr ptr = nullptr;

	void drop();

public:
	GodotRef() {}
	// Takes over one reference the caller already holds (a ptrcall return, or a fresh construct()).
	static GodotRef adopt(GDExtensionObjectPtr p_ptr) {
		GodotRef ref;
		ref.ptr = p_ptr;
		return ref;
	}
	// Constructs a new RefCounted engine object of the named class.
	static GodotRef construct(const char *p_class);
	GodotRef(const GodotRef &p_other);
	GodotRef(GodotRef &&p_other) :
			ptr(p_other.ptr) {
		p_other.ptr = nullptr;
	}
	GodotRef &operator=(const GodotRef &p_other);
	GodotRef &operator=(GodotRef &&p_other) {
		if (this != &p_other) {
			drop();
			ptr = p_other.ptr;
			p_other.ptr = nullptr;
		}
		return *this;
	}
	~GodotRef() { drop(); }
	GDExtensionObjectPtr get() const { return ptr; }
};

// Constructs a non-reference-counted engine object (a Node). The caller owns it until it is added to the tree.
GDExtensionObjectPtr construct_object(const char *p_class);

struct StringHasher {
	size_t operator()(const godot::String &p_string) const { return (size_t)p_string.hash(); }
};
using Object_ = Dict<godot::String, StringHasher>;

// A Godot value with no AS3 primitive form (Vector2, Object, RID), held in an untyped AS3 value.
struct BoxedVariant : Object {
	godot::Variant value;
	static const Class *class_info();
	static BoxedVariant *create(const godot::Variant &p_value);
};

// Collection conversions at the engine boundary (rule 7). Each copies eagerly and in full.
godot::Array to_godot_array(const Array *p_array);
godot::PackedVector2Array to_packed_vector2(const Vector<godot::Vector2> *p_vector);
Ref<Object_> from_godot_dictionary(const godot::Dictionary &p_dictionary);
Ref<Array> from_godot_array(const godot::Array &p_array);
godot::Variant to_variant(const Value &p_value);
Value from_variant(const godot::Variant &p_variant);

// Setup helpers, never on a measured path.
void connect_method(GDExtensionObjectPtr p_owner, const char *p_signal, const char *p_method);
void add_child(GDExtensionObjectPtr p_parent, GDExtensionObjectPtr p_child);
void attach_script(GDExtensionObjectPtr p_object, const char *p_class);
// The AS3 object behind a script instance the host created, or null.
Object *script_object_of(GDExtensionObjectPtr p_owner);

// Prints "<Type>: <message>" and the AS3 stack trace through the engine, then clears the error.
void report_uncaught();

void godot_calls_init();

} // namespace as3
