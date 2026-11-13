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

// A handle on a reference-counted object that is gone. Copying it would count a reference on freed
// memory. (Only a script's hold on its own owner can be one: any other handle keeps its object alive.) A
// handle on any other freed object is safe to copy, and Godot knows it as a freed object.
static bool is_dangling(const Variant &p_value) {
	return p_value.get_type() == Variant::OBJECT && ((ObjectID)p_value).is_ref_counted() && !p_value.get_validated_object();
}

static String to_godot(std::string_view p_text) {
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
			return string ? Variant(to_godot(string->view())) : Variant();
		}
		case ir::Kind::Val:
		case ir::Kind::Ref: {
			const Variant *value = static_cast<const Variant *>(p_slot.p);
			if (!value || is_dangling(*value)) {
				return Variant();
			}
			return *value;
		}
		default:
			return Variant();
	}
}

bool arrives(ir::Kind p_kind, std::string_view p_godot_type, const Variant &p_value) {
	Variant::Type type = p_value.get_type();
	bool number = type == Variant::INT || type == Variant::FLOAT;
	switch (p_kind) {
		case ir::Kind::F64:
			return number;
		case ir::Kind::I32:
		case ir::Kind::U32: {
			if (!number) {
				return false;
			}
			double value = (double)p_value;
			double low = p_kind == ir::Kind::I32 ? -2147483648.0 : 0.0;
			double high = p_kind == ir::Kind::I32 ? 2147483647.0 : 4294967295.0;
			return value >= low && value <= high && (type == Variant::FLOAT || (int64_t)p_value == (int64_t)value);
		}
		case ir::Kind::Bool:
			return type == Variant::BOOL;
		case ir::Kind::Str:
			return type == Variant::STRING || type == Variant::STRING_NAME || type == Variant::NODE_PATH || type == Variant::NIL;
		case ir::Kind::Ref: {
			// No object: null itself, or an object-typed value that holds none (what get_node_or_null gives).
			if (type == Variant::NIL || (type == Variant::OBJECT && ((ObjectID)p_value).is_null())) {
				return true;
			}
			Object *object = type == Variant::OBJECT ? p_value.get_validated_object() : nullptr;
			return object && (p_godot_type.empty() || object->is_class(to_godot(p_godot_type)));
		}
		case ir::Kind::Val:
			// The value type that is named, and no other: a Vector3 is not a Vector2. Nothing at all is null.
			if (type == Variant::OBJECT) {
				return false;
			}
			return type == Variant::NIL || p_godot_type.empty() || Variant::get_type_name(type) == to_godot(p_godot_type);
		default:
			return false; // A script object can't come from Godot.
	}
}

// What Godot gave, as the interpreter asked for it; refused if it is something else.
static bool take(const vm::Host::Wanted &p_wanted, const Variant &p_value, std::string_view p_from, vm::Slot *r_result, std::string *r_error) {
	if (!r_result || p_wanted.kind == ir::Kind::Void) {
		return true;
	}
	if (!arrives(p_wanted.kind, p_wanted.type, p_value)) {
		CharString got = Variant::get_type_name(p_value.get_type()).utf8();
		if ((p_wanted.kind == ir::Kind::I32 || p_wanted.kind == ir::Kind::U32) && (p_value.get_type() == Variant::INT || p_value.get_type() == Variant::FLOAT)) {
			CharString number = p_value.stringify().utf8();
			*r_error = "Godot's '" + std::string(p_from) + "' gave " + std::string(number.get_data()) + ", which does not fit " +
					(p_wanted.kind == ir::Kind::I32 ? "an int" : "a uint");
			return false;
		}
		*r_error = "Godot's '" + std::string(p_from) + "' gave a " + std::string(got.get_data()) + " where " +
				(p_wanted.type.empty() ? std::string(ir::kind_name(p_wanted.kind)) : std::string(p_wanted.type)) + " was expected";
		return false;
	}
	*r_result = from_variant(p_wanted.kind, p_value);
	return true;
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
			// An object-typed value that holds no object is null, like null itself.
			if (p_value.get_type() != Variant::NIL &&
					!(p_value.get_type() == Variant::OBJECT && ((ObjectID)p_value).is_null())) {
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
	UtilityFunctions::print(to_godot(p_text));
}

void GodotHost::error(std::string_view p_text) {
	UtilityFunctions::push_error(to_godot(p_text));
}

void *GodotHost::construct(std::string_view p_type, const Argument *p_arguments, size_t p_count, std::string *r_error) {
	String wanted = to_godot(p_type);
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

bool GodotHost::call(void *p_receiver, std::string_view p_method, const Argument *p_arguments, size_t p_count, const Wanted &p_result,
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
	receiver->callp(StringName(to_godot(p_method)), pointers.data(), (int)p_count, result, error);
	if (error.error != GDEXTENSION_CALL_OK) {
		*r_error = describe(error, p_method);
		return false;
	}
	return take(p_result, result, p_method, r_result, r_error);
}

// AS3's spelling of an operator, as Godot's. With one operand, '-' and '+' are the unary ones.
static bool operator_of(std::string_view p_operator, size_t p_count, Variant::Operator *r_operator) {
	static const struct {
		const char *spelling;
		Variant::Operator op;
	} TABLE[] = {
		{ "==", Variant::OP_EQUAL }, { "!=", Variant::OP_NOT_EQUAL }, { "<", Variant::OP_LESS }, { "<=", Variant::OP_LESS_EQUAL },
		{ ">", Variant::OP_GREATER }, { ">=", Variant::OP_GREATER_EQUAL }, { "+", Variant::OP_ADD }, { "-", Variant::OP_SUBTRACT },
		{ "*", Variant::OP_MULTIPLY }, { "/", Variant::OP_DIVIDE }, { "%", Variant::OP_MODULE }, { "<<", Variant::OP_SHIFT_LEFT },
		{ ">>", Variant::OP_SHIFT_RIGHT }, { "&", Variant::OP_BIT_AND }, { "|", Variant::OP_BIT_OR }, { "^", Variant::OP_BIT_XOR },
	};
	if (p_count == 1) {
		if (p_operator == "-" || p_operator == "+") {
			*r_operator = p_operator == "-" ? Variant::OP_NEGATE : Variant::OP_POSITIVE;
			return true;
		}
		if (p_operator == "~") {
			*r_operator = Variant::OP_BIT_NEGATE;
			return true;
		}
		return false;
	}
	for (const auto &entry : TABLE) {
		if (p_operator == entry.spelling) {
			*r_operator = entry.op;
			return true;
		}
	}
	return false;
}

bool GodotHost::operate(std::string_view p_operator, const Argument *p_arguments, size_t p_count, const Wanted &p_result,
		vm::Slot *r_result, std::string *r_error) {
	Variant::Operator op = Variant::OP_ADD;
	if (p_count < 1 || p_count > 2 || !operator_of(p_operator, p_count, &op)) {
		*r_error = "Godot has no operator '" + std::string(p_operator) + "'";
		return false;
	}
	Variant left = to_variant(p_arguments[0].kind, p_arguments[0].slot);
	Variant right = p_count == 2 ? to_variant(p_arguments[1].kind, p_arguments[1].slot) : Variant();
	Variant result;
	bool valid = false;
	Variant::evaluate(op, left, right, result, valid);
	if (!valid && (op == Variant::OP_EQUAL || op == Variant::OP_NOT_EQUAL)) {
		// Two things Godot can't compare are not equal, as in AS3: `vector == 5` is false.
		result = op == Variant::OP_NOT_EQUAL;
		valid = true;
	}
	if (!valid) {
		CharString a = Variant::get_type_name(left.get_type()).utf8();
		CharString b = Variant::get_type_name(right.get_type()).utf8();
		*r_error = p_count == 2 ? "Godot can't work out " + std::string(a.get_data()) + " " + std::string(p_operator) + " " + std::string(b.get_data()) + " for these values"
								: "Godot can't work out " + std::string(p_operator) + std::string(a.get_data());
		return false;
	}
	return take(p_result, result, p_operator, r_result, r_error);
}

bool GodotHost::get(void *p_receiver, std::string_view p_property, const Wanted &p_result, vm::Slot *r_result, std::string *r_error) {
	bool valid = false;
	Variant value = static_cast<Variant *>(p_receiver)->get_named(StringName(to_godot(p_property)), valid);
	if (!valid) {
		*r_error = "Godot has no '" + std::string(p_property) + "' on this value";
		return false;
	}
	return take(p_result, value, p_property, r_result, r_error);
}

bool GodotHost::set(void *p_receiver, std::string_view p_property, const Argument &p_value, std::string *r_error) {
	bool valid = false;
	static_cast<Variant *>(p_receiver)->set_named(StringName(to_godot(p_property)), to_variant(p_value.kind, p_value.slot), valid);
	if (!valid) {
		*r_error = "Godot could not set '" + std::string(p_property) + "' on this value";
		return false;
	}
	return true;
}

// "element 5 of this Vector2", for an error.
static std::string element(const Variant &p_key, const Variant &p_of) {
	CharString key = p_key.stringify().utf8();
	CharString type = Variant::get_type_name(p_of.get_type()).utf8();
	return "element " + std::string(key.get_data()) + " of this " + std::string(type.get_data());
}

bool GodotHost::index_get(void *p_receiver, const Argument &p_key, const Wanted &p_result, vm::Slot *r_result, std::string *r_error) {
	const Variant *receiver = static_cast<Variant *>(p_receiver);
	Variant key = to_variant(p_key.kind, p_key.slot);
	bool valid = false;
	Variant value = receiver->get(key, &valid);
	if (!valid) {
		*r_error = "Godot has no " + element(key, *receiver);
		return false;
	}
	return take(p_result, value, "[]", r_result, r_error);
}

bool GodotHost::index_set(void *p_receiver, const Argument &p_key, const Argument &p_value, std::string *r_error) {
	Variant *receiver = static_cast<Variant *>(p_receiver);
	Variant key = to_variant(p_key.kind, p_key.slot);
	bool valid = false;
	receiver->set(key, to_variant(p_value.kind, p_value.slot), &valid);
	if (!valid) {
		*r_error = "Godot could not set " + element(key, *receiver);
		return false;
	}
	return true;
}

vm::Str *GodotHost::text(void *p_value) {
	CharString utf8 = static_cast<Variant *>(p_value)->stringify().utf8();
	return vm::new_string(std::string_view(utf8.get_data(), (size_t)utf8.length()));
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
	if (is_dangling(*value)) {
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
