#include "godot_host.h"

#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <godot_cpp/classes/ref_counted.hpp>

#include <new>
#include <vector>

using namespace godot;

namespace kas3 {

static String text(std::string_view p_text) {
	return String::utf8(p_text.data(), (int64_t)p_text.size());
}

Variant to_variant(ir::Kind p_kind, const vm::Slot &p_slot) {
	switch (p_kind) {
		case ir::Kind::I32:
			return Variant((int64_t)p_slot.i);
		case ir::Kind::U32:
			return Variant((int64_t)p_slot.u);
		case ir::Kind::F64:
			return Variant(p_slot.d);
		case ir::Kind::Bool:
			return Variant(p_slot.b);
		case ir::Kind::Str: {
			const vm::Str *string = static_cast<const vm::Str *>(p_slot.p);
			return string ? Variant(text(string->view())) : Variant();
		}
		case ir::Kind::Val:
		case ir::Kind::Ref:
			return p_slot.p ? *static_cast<const Variant *>(p_slot.p) : Variant();
		default:
			return Variant();
	}
}

vm::Slot from_variant(ir::Kind p_kind, const Variant &p_value) {
	vm::Slot slot;
	slot.bits = 0;
	switch (p_kind) {
		case ir::Kind::I32:
			slot.i = (int32_t)(int64_t)p_value;
			break;
		case ir::Kind::U32:
			slot.u = (uint32_t)(int64_t)p_value;
			break;
		case ir::Kind::F64:
			slot.d = (double)p_value;
			break;
		case ir::Kind::Bool:
			slot.b = (bool)p_value;
			break;
		case ir::Kind::Str:
			if (p_value.get_type() != Variant::NIL) {
				CharString utf8 = String(p_value).utf8();
				slot.p = vm::new_string(std::string_view(utf8.get_data(), (size_t)utf8.length()));
			}
			break;
		case ir::Kind::Val:
		case ir::Kind::Ref:
			if (p_value.get_type() != Variant::NIL) {
				slot.p = new Variant(p_value);
			}
			break;
		default:
			break;
	}
	return slot;
}

static std::string describe(const GDExtensionCallError &p_error, std::string_view p_what) {
	std::string what(p_what);
	switch (p_error.error) {
		case GDEXTENSION_CALL_ERROR_INVALID_METHOD:
			return "Godot has no '" + what + "' on this value";
		case GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT:
			return "Godot rejected argument " + std::to_string(p_error.argument + 1) + " of '" + what + "'";
		case GDEXTENSION_CALL_ERROR_TOO_MANY_ARGUMENTS:
			return "too many arguments for Godot's '" + what + "'";
		case GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS:
			return "too few arguments for Godot's '" + what + "'";
		case GDEXTENSION_CALL_ERROR_INSTANCE_IS_NULL:
			return "'" + what + "' was called on an object Godot has freed";
		default:
			return "Godot could not call '" + what + "'";
	}
}

void GodotHost::print(std::string_view p_text) {
	UtilityFunctions::print(text(p_text));
}

void GodotHost::error(std::string_view p_text) {
	UtilityFunctions::push_error(text(p_text));
}

void *GodotHost::construct(std::string_view p_type, const Argument *p_arguments, size_t p_count, std::string *r_error) {
	String wanted = text(p_type);
	int type = -1;
	for (int i = 0; i < Variant::VARIANT_MAX; i++) {
		if (Variant::get_type_name((Variant::Type)i) == wanted) {
			type = i;
			break;
		}
	}
	if (type < 0) {
		*r_error = "Godot has no value type '" + std::string(p_type) + "'";
		return nullptr;
	}
	std::vector<Variant> values;
	std::vector<GDExtensionConstVariantPtr> pointers;
	values.reserve(p_count);
	for (size_t i = 0; i < p_count; i++) {
		values.push_back(to_variant(p_arguments[i].kind, p_arguments[i].slot));
	}
	for (size_t i = 0; i < p_count; i++) {
		pointers.push_back(values[i]._native_ptr());
	}
	Variant *made = new Variant();
	GDExtensionCallError error;
	error.error = GDEXTENSION_CALL_OK;
	gdextension_interface::variant_construct((GDExtensionVariantType)type, made->_native_ptr(), pointers.data(), (int32_t)p_count, &error);
	if (error.error != GDEXTENSION_CALL_OK) {
		delete made;
		*r_error = describe(error, "new " + std::string(p_type));
		return nullptr;
	}
	return made;
}

bool GodotHost::call(void *p_receiver, std::string_view p_method, const Argument *p_arguments, size_t p_count, ir::Kind p_result,
		vm::Slot *r_result, std::string *r_error) {
	Variant *receiver = static_cast<Variant *>(p_receiver);
	// An object Godot has freed leaves a dangling pointer in the handle: ask Godot whether it is still
	// there before anything touches it.
	// (A handle copied after the object went is empty: see copy().)
	if (receiver->get_type() == Variant::NIL || (receiver->get_type() == Variant::OBJECT && !receiver->get_validated_object())) {
		*r_error = "'" + std::string(p_method) + "' was called on an object Godot has freed";
		return false;
	}
	std::vector<Variant> values;
	std::vector<const Variant *> pointers;
	values.reserve(p_count);
	for (size_t i = 0; i < p_count; i++) {
		values.push_back(to_variant(p_arguments[i].kind, p_arguments[i].slot));
	}
	for (size_t i = 0; i < p_count; i++) {
		pointers.push_back(&values[i]);
	}
	Variant result;
	GDExtensionCallError error;
	error.error = GDEXTENSION_CALL_OK;
	receiver->callp(StringName(text(p_method)), pointers.data(), (int)p_count, result, error);
	if (error.error != GDEXTENSION_CALL_OK) {
		*r_error = describe(error, p_method);
		return false;
	}
	if (r_result) {
		*r_result = from_variant(p_result, result);
	}
	return true;
}

bool GodotHost::get(void *p_receiver, std::string_view p_property, ir::Kind p_result, vm::Slot *r_result, std::string *r_error) {
	bool valid = false;
	Variant value = static_cast<Variant *>(p_receiver)->get_named(StringName(text(p_property)), valid);
	if (!valid) {
		*r_error = "Godot has no '" + std::string(p_property) + "' on this value";
		return false;
	}
	*r_result = from_variant(p_result, value);
	return true;
}

bool GodotHost::set(void *p_receiver, std::string_view p_property, const Argument &p_value, std::string *r_error) {
	bool valid = false;
	static_cast<Variant *>(p_receiver)->set_named(StringName(text(p_property)), to_variant(p_value.kind, p_value.slot), valid);
	if (!valid) {
		*r_error = "Godot could not set '" + std::string(p_property) + "' on this value";
		return false;
	}
	return true;
}

void *GodotHost::borrow(Object *p_object) {
	// A Variant holding a reference-counted object counts as a reference to it. This one must not, so its
	// reference is given back at once, and drop() frees the memory without running the Variant's
	// destructor, which would give it back a second time. A Variant holding an object owns no other memory.
	void *memory = ::operator new(sizeof(Variant));
	new (memory) Variant(p_object);
	if (RefCounted *counted = Object::cast_to<RefCounted>(p_object)) {
		counted->unreference();
	}
	borrowed_.insert(memory);
	return memory;
}

void *GodotHost::copy(void *p_value) {
	if (!p_value) {
		return nullptr;
	}
	Variant *value = static_cast<Variant *>(p_value);
	// A copy of an object handle whose object is gone would try to count a reference on freed memory.
	if (value->get_type() == Variant::OBJECT && !value->get_validated_object()) {
		return new Variant();
	}
	return new Variant(*value);
}

void GodotHost::drop(void *p_value) {
	if (!p_value) {
		return;
	}
	auto borrowed = borrowed_.find(p_value);
	if (borrowed != borrowed_.end()) {
		borrowed_.erase(borrowed);
		::operator delete(p_value);
		return;
	}
	delete static_cast<Variant *>(p_value);
}

} // namespace kas3
