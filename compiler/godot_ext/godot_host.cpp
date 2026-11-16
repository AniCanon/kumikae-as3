#include "godot_host.h"
#include "godot_calls.h"

#include "any.h"

#include "builtins.h"
#include "callable.h"
#include "members.h"
#include "program.h"
#include "handles.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/expression.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/signal.hpp>

#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <godot_cpp/classes/ref_counted.hpp>

#include <cmath>
#include <cstdlib>
#include <new>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace godot;

namespace kas3 {

String to_godot(std::string_view p_text) {
	return String::utf8(p_text.data(), (int64_t)p_text.size());
}

std::string describe(const GDExtensionCallError &p_error, std::string_view p_what) {
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
			return FREED;
		default:
			return "Godot could not call '" + what + "'";
	}
}

Variant to_variant(ir::Kind p_kind, const vm::Slot &p_slot) {
	switch (p_kind) {
		case ir::Kind::I32:
			return Variant((int64_t)p_slot.i);
		case ir::Kind::U32:
			return Variant((int64_t)p_slot.u);
		case ir::Kind::I64:
			return Variant(p_slot.l);
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
			// A freed object goes to Godot as what it is to the script: null.
			if (!value || is_freed(*value)) {
				return Variant();
			}
			return *value;
		}
		case ir::Kind::Any:
			return untyped_to_variant(p_slot);
		default:
			return Variant();
	}
}

namespace {

// Godot's value type of this name ("Vector2"), found without making text: the names are read once.
std::optional<Variant::Type> value_type_named(std::string_view p_name) {
	static const std::vector<std::pair<std::string, Variant::Type>> types = [] {
		std::vector<std::pair<std::string, Variant::Type>> all;
		for (int type = 0; type < Variant::VARIANT_MAX; type++) {
			const CharString name = Variant::get_type_name((Variant::Type)type).utf8();
			all.emplace_back(std::string(name.get_data(), (size_t)name.length()), (Variant::Type)type);
		}
		return all;
	}();
	for (const auto &[name, type] : types) {
		if (name == p_name) {
			return type;
		}
	}
	return std::nullopt;
}

// Godot's text for a class name the program declares, made once: checking what arrives builds no text.
const String &class_text(std::string_view p_name) {
	static std::unordered_map<std::string, String> made;
	const auto [entry, added] = made.try_emplace(std::string(p_name));
	if (added) {
		entry->second = to_godot(p_name);
	}
	return entry->second;
}

} // namespace

bool arrives(ir::Kind p_kind, std::string_view p_godot_type, const Variant &p_value) {
	Variant::Type type = p_value.get_type();
	bool number = type == Variant::INT || type == Variant::FLOAT;
	switch (p_kind) {
		case ir::Kind::F64: {
			// One of Godot's integers is taken as a Number only if the Number holds the very same value.
			if (type != Variant::INT) {
				return number;
			}
			int64_t value = (int64_t)p_value;
			double as_number = (double)value;
			return as_number < 9223372036854775808.0 && (int64_t)as_number == value;
		}
		case ir::Kind::I64: {
			if (type == Variant::INT) {
				return true; // An Int64 is Godot's own integer.
			}
			double value = type == Variant::FLOAT ? (double)p_value : 0.5;
			return value >= -9223372036854775808.0 && value < 9223372036854775808.0 && value == (double)(int64_t)value;
		}
		case ir::Kind::I32:
		case ir::Kind::U32: {
			if (!number) {
				return false;
			}
			double value = (double)p_value;
			double low = p_kind == ir::Kind::I32 ? -2147483648.0 : 0.0;
			double high = p_kind == ir::Kind::I32 ? 2147483647.0 : 4294967295.0;
			// A fraction is not a whole number: 3.0 is taken, 2.5 is not.
			return value >= low && value <= high && (type == Variant::FLOAT ? value == (double)(int64_t)value : (int64_t)p_value == (int64_t)value);
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
			if (type == Variant::OBJECT && !object) {
				return true; // An object that has been freed: null, as it is to a script everywhere.
			}
			return object && (p_godot_type.empty() || object->is_class(class_text(p_godot_type)));
		}
		case ir::Kind::Any:
			return true; // Anything at all.
		case ir::Kind::Val:
			// The value type that is named, and no other: a Vector3 is not a Vector2. Nothing at all is null.
			if (type == Variant::OBJECT) {
				return false;
			}
			return type == Variant::NIL || p_godot_type.empty() || value_type_named(p_godot_type) == type;
		default:
			return false; // A script object can't come from Godot.
	}
}

bool take(const vm::Wanted &p_wanted, const Variant &p_value, std::string_view p_from, vm::Slot *r_result, std::string *r_error) {
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
		if (p_wanted.kind == ir::Kind::F64 && p_value.get_type() == Variant::INT) {
			// What a typed Int64 gives in the same place: a whole number a Number can't hold exactly.
			*r_error = "RangeError: " + std::to_string((int64_t)p_value) + " does not fit a Number exactly.";
			return false;
		}
		*r_error = "Godot's '" + std::string(p_from) + "' gave a " + std::string(got.get_data()) + " where " +
				(p_wanted.type.empty() ? std::string(ir::kind_name(p_wanted.kind)) : std::string(p_wanted.type)) + " was expected";
		return false;
	}
	const std::optional<vm::Slot> taken = from_variant(p_wanted.kind, p_value);
	if (!taken) {
		*r_error = std::string(vm::OUT_OF_MEMORY);
		return false;
	}
	*r_result = *taken;
	return true;
}

std::optional<vm::Slot> from_variant(ir::Kind p_kind, const Variant &p_value) {
	vm::Slot slot;
	slot.bits = 0;
	switch (p_kind) {
		case ir::Kind::I32:
			slot.i = (int32_t)(int64_t)p_value;
			break;
		case ir::Kind::U32:
			slot.u = (uint32_t)(int64_t)p_value;
			break;
		case ir::Kind::I64:
			slot.l = p_value.get_type() == Variant::FLOAT ? (int64_t)(double)p_value : (int64_t)p_value;
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
				if (!slot.p) {
					return std::nullopt;
				}
			}
			break;
		case ir::Kind::Any:
			return untyped_from_variant(p_value);
		case ir::Kind::Val:
		case ir::Kind::Ref:
			// An object-typed value that holds no object, or one that has been freed, is null, like null itself.
			if (p_value.get_type() != Variant::NIL &&
					!(p_value.get_type() == Variant::OBJECT && (((ObjectID)p_value).is_null() || !p_value.get_validated_object()))) {
				slot.p = make_handle(p_value);
			}
			break;
		default:
			break;
	}
	return slot;
}



const StringName &GodotHost::name(std::string_view p_text) {
	Name &entry = names_[Place{ p_text.data(), p_text.size() }];
	if (!entry.made || entry.text != p_text) {
		entry.text.assign(p_text.data(), p_text.size());
		entry.godot_text = to_godot(p_text);
		entry.godot = StringName(entry.godot_text);
		entry.made = true;
	}
	return entry.godot;
}

const String &GodotHost::text_of(std::string_view p_text) {
	name(p_text);
	return names_[Place{ p_text.data(), p_text.size() }].godot_text;
}

void GodotHost::print(std::string_view p_text) {
	UtilityFunctions::print(to_godot(p_text));
}

// An error with no place it was thrown: there is no line for the editor to open.
void GodotHost::error(std::string_view p_text) {
	UtilityFunctions::push_error(to_godot(p_text));
}

// Shown as Godot shows a script's own error: the script's file and line are given as the error's source,
// so that the editor can open the line.
void GodotHost::error_at(std::string_view p_text, std::string_view p_function, std::string_view p_file, int32_t p_line) {
	const std::string text(p_text), function(p_function), file(p_file);
	gdextension_interface::print_script_error(text.c_str(), function.c_str(), file.c_str(), p_line, false);
}


bool GodotHost::call(void *p_receiver, std::string_view p_method, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_result,
		vm::Slot *r_result, std::string *r_error) {
	Variant *receiver = static_cast<Variant *>(p_receiver);
	if (!p_method.empty() && p_method[0] == '@') {
		return signal_verb(*receiver, p_method, p_arguments, p_count, p_result, r_result, r_error);
	}
	// An object Godot has freed leaves a dangling pointer in the handle: ask Godot whether it is still
	// there before anything touches it.
	// (A handle copied after the object went is empty: see copy().)
	if (receiver->get_type() == Variant::NIL || (receiver->get_type() == Variant::OBJECT && !receiver->get_validated_object())) {
		*r_error = FREED;
		return false;
	}
	Arguments arguments(p_arguments, p_count);
	Variant result;
	GDExtensionCallError error;
	error.error = GDEXTENSION_CALL_OK;
	receiver->callp(name(p_method), arguments.pointers(), (int)p_count, result, error);
	if (error.error != GDEXTENSION_CALL_OK) {
		*r_error = describe(error, p_method);
		return false;
	}
	return take(p_result, result, p_method, r_result, r_error);
}

// AS3's spelling of an operator, as Godot's. With one operand, '-' and '+' are the unary ones.
static std::optional<Variant::Operator> operator_of(std::string_view p_operator, size_t p_count) {
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
			return p_operator == "-" ? Variant::OP_NEGATE : Variant::OP_POSITIVE;
		}
		if (p_operator == "~") {
			return Variant::OP_BIT_NEGATE;
		}
		return std::nullopt;
	}
	for (const auto &entry : TABLE) {
		if (p_operator == entry.spelling) {
			return entry.op;
		}
	}
	return std::nullopt;
}

bool GodotHost::operate(std::string_view p_operator, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_result,
		vm::Slot *r_result, std::string *r_error) {
	const std::optional<Variant::Operator> op = p_count < 1 || p_count > 2 ? std::nullopt : operator_of(p_operator, p_count);
	if (!op) {
		*r_error = "Godot has no operator '" + std::string(p_operator) + "'";
		return false;
	}
	Variant left = to_variant(p_arguments[0].kind, p_arguments[0].slot);
	Variant right = p_count == 2 ? to_variant(p_arguments[1].kind, p_arguments[1].slot) : Variant();
	Variant result;
	bool valid = false;
	Variant::evaluate(*op, left, right, result, valid);
	if (!valid && (*op == Variant::OP_EQUAL || *op == Variant::OP_NOT_EQUAL)) {
		// Two things Godot can't compare are not equal, as in AS3: `vector == 5` is false.
		result = *op == Variant::OP_NOT_EQUAL;
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

bool GodotHost::get(void *p_receiver, std::string_view p_property, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) {
	if (!p_property.empty() && p_property[0] == '@') {
		return signal_verb(*static_cast<Variant *>(p_receiver), p_property, nullptr, 0, p_result, r_result, r_error);
	}
	if (is_freed(*static_cast<Variant *>(p_receiver))) {
		*r_error = FREED;
		return false;
	}
	bool valid = false;
	Variant value = static_cast<Variant *>(p_receiver)->get_named(name(p_property), valid);
	if (!valid) {
		*r_error = "Godot has no '" + std::string(p_property) + "' on this value";
		return false;
	}
	return take(p_result, value, p_property, r_result, r_error);
}

bool GodotHost::set(void *p_receiver, std::string_view p_property, const vm::Argument &p_value, std::string *r_error) {
	if (is_freed(*static_cast<Variant *>(p_receiver))) {
		*r_error = FREED;
		return false;
	}
	bool valid = false;
	static_cast<Variant *>(p_receiver)->set_named(name(p_property), to_variant(p_value.kind, p_value.slot), valid);
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

// A Number as an index of something indexed by number: it must be a whole one, as for AS3's Vector (Godot
// would cut 1.5 to 1, and NaN to 0). It goes as Godot's integer. False, with AS3's RangeError, if not.
static bool whole_index(const Variant &p_receiver, Variant *r_key, std::string *r_error) {
	if (r_key->get_type() != Variant::FLOAT || p_receiver.get_type() == Variant::DICTIONARY || p_receiver.get_type() == Variant::OBJECT) {
		return true;
	}
	const double index = *r_key;
	if (index == std::trunc(index) && index >= -9223372036854775808.0 && index < 9223372036854775808.0) {
		*r_key = (int64_t)index;
		return true;
	}
	Variant copy = p_receiver;
	const Variant size = copy.has_method("size") ? copy.call("size") : Variant(); // A Vector2 has none.
	*r_error = "RangeError: Error #1125: The index " + vm::number_to_string(index) + " is out of range" +
			(size.get_type() == Variant::INT ? " " + std::to_string((int64_t)size) : std::string()) + ".";
	return false;
}

bool GodotHost::index_get(void *p_receiver, const vm::Argument &p_key, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) {
	const Variant *receiver = static_cast<Variant *>(p_receiver);
	Variant key = to_variant(p_key.kind, p_key.slot);
	if (!whole_index(*receiver, &key, r_error)) {
		return false;
	}
	bool valid = false;
	Variant value = receiver->get(key, &valid);
	if (!valid) {
		*r_error = "Godot has no " + element(key, *receiver);
		return false;
	}
	return take(p_result, value, "[]", r_result, r_error);
}

bool GodotHost::index_set(void *p_receiver, const vm::Argument &p_key, const vm::Argument &p_value, std::string *r_error) {
	Variant *receiver = static_cast<Variant *>(p_receiver);
	Variant key = to_variant(p_key.kind, p_key.slot);
	if (!whole_index(*receiver, &key, r_error)) {
		return false;
	}
	bool valid = false;
	receiver->set(key, to_variant(p_value.kind, p_value.slot), &valid);
	if (!valid) {
		*r_error = "Godot could not set " + element(key, *receiver);
		return false;
	}
	return true;
}

void *GodotHost::bind(vm::Object *p_object, uint32_t p_function, void *p_owner, std::string *r_error) {
	Object *owner = nullptr;
	if (p_owner) {
		owner = static_cast<Variant *>(p_owner)->get_validated_object();
		if (!owner) {
			*r_error = FREED;
			return nullptr;
		}
	}
	return make_callable(p_object, p_function, owner);
}

bool GodotHost::member(void *p_receiver, std::string_view p_name, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) {
	Object *object = static_cast<Variant *>(p_receiver)->get_validated_object();
	if (!object) {
		*r_error = FREED;
		return false;
	}
	// The name is AS3's. Godot's own comes from the name table, for the object's class or one it extends.
	std::optional<std::string> godot;
	for (StringName at = object->get_class(); !godot && !String(at).is_empty(); at = ClassDB::get_parent_class(at)) {
		CharString klass = String(at).utf8();
		godot = godot_name(std::string_view(klass.get_data()) == "Object" ? "GodotObject" : klass.get_data(), p_name);
	}
	const bool found = godot.has_value();
	// Not one of Godot's: a public field of the AS3 script attached to it, as Godot itself would reach it.
	const vm::Object *script = found ? nullptr : script_object(object);
	const Member field = script ? find_member(script->klass, StringName(String::utf8(p_name.data(), (int64_t)p_name.size()))) : Member{};
	if (!found && field.what != Member::What::Field) {
		CharString klass = String(object->get_class()).utf8();
		*r_error = "a " + std::string(klass.get_data()) + " has no '" + std::string(p_name) + "'";
		return false;
	}
	const Variant value = found ? object->get(StringName(to_godot(*godot))) : read_field(script, field.index);
	if (p_result.type == "Signal" && value.get_type() != Variant::SIGNAL) {
		*r_error = "'" + std::string(p_name) + "' is not a signal";
		return false;
	}
	return take(p_result, value, p_name, r_result, r_error);
}

} // namespace kas3
