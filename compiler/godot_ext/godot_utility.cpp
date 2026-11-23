// Godot's global functions (lerpf, print, randf): called through the entry Godot gives extensions for
// them, which takes arguments of exact types and not boxed values. The interpreter passes the function's
// signature, written by the generator: "lerpf 998901048 float float,float,float", and "varargs" at the
// end for a function that takes any number of arguments.
#include "godot_calls.h"
#include "godot_host.h"

#include "any.h"
#include "worked.h"

#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <optional>
#include <vector>

using namespace godot;

namespace kas3 {

namespace {

// A parameter or result declared as Variant.
constexpr int ANYTHING = -1;
// No result.
constexpr int NOTHING = -2;

// A value of one of Godot's types as the entry takes it: the bare value, not a Variant. Large enough for
// every type a global function uses (the largest are the packed arrays, two words).
struct alignas(16) Bare {
	unsigned char bytes[64];
};

// One of the signature's type names; nothing for a name Godot does not have.
std::optional<int> type_of_name(std::string_view p_name) {
	if (p_name == "Variant") {
		return ANYTHING;
	}
	if (p_name == "void") {
		return NOTHING;
	}
	if (p_name == "Object") {
		return Variant::OBJECT;
	}
	return value_type(p_name);
}

// Whether a value may be given where a global function declares this type. Godot's own conversion would
// turn text into 0 and a fraction into a whole number without a word; this is what stops that.
bool fits(const Variant &p_value, int p_type) {
	const Variant::Type given = p_value.get_type();
	switch (p_type) {
		case ANYTHING:
			return true;
		case Variant::FLOAT:
			return given == Variant::FLOAT || given == Variant::INT;
		case Variant::INT:
			// A Number for an integer is converted, or refused, before the call.
			return given == Variant::INT;
		case Variant::STRING:
			return given == Variant::STRING || given == Variant::STRING_NAME;
		case Variant::OBJECT:
			return given == Variant::OBJECT || given == Variant::NIL;
		default:
			return (int)given == p_type;
	}
}

// An argument of a function that writes text: null (a freed object is null) and undefined in AS3's
// words, as C#'s GD.Str and GD.Print write a null, not Godot's `<null>`.
Variant text_argument(const vm::Argument &p_argument) {
	Variant value = to_variant(p_argument.kind, p_argument.slot);
	if (value.get_type() != Variant::NIL) {
		return value;
	}
	const bool undefined = p_argument.kind == ir::Kind::Any && vm::any::tag(p_argument.slot) == vm::AnyTag::Undefined;
	return to_godot(undefined ? std::string_view("undefined") : NULL_TEXT);
}

std::string type_name(int p_type) {
	return p_type == ANYTHING ? "anything" : Variant::get_type_name((Variant::Type)p_type).utf8().get_data();
}

} // namespace

struct GodotHost::Utility {
	// The key: the text the interpreter passes.
	std::string signature;
	std::string name;
	GDExtensionPtrUtilityFunction function = nullptr;
	int result = NOTHING;
	std::vector<int> parameters;
	bool any_number = false;
	// Null is a proper answer from it.
	bool reads_a_value = false;
	// It makes text of any number of arguments: str, print, push_error and the rest, the functions of any
	// number of arguments that give back text or nothing.
	bool writes_text = false;
};

GodotHost::GodotHost() = default;
GodotHost::~GodotHost() = default;

const GodotHost::Utility *GodotHost::utility(std::string_view p_signature, std::string *r_error) {
	auto found = utilities_.find(p_signature);
	if (found != utilities_.end()) {
		return found->second.get();
	}
	// "name hash result parameters [varargs]".
	const std::vector<std::string_view> parts = godot_words(p_signature);
	auto made = std::make_unique<Utility>();
	made->signature = std::string(p_signature);
	if (parts.size() < 4) {
		*r_error = "'" + made->signature + "' is not a global function of Godot's";
		return nullptr;
	}
	made->name = std::string(parts[0]);
	const std::optional<int> result = type_of_name(parts[2]);
	if (!result) {
		*r_error = "Godot's '" + made->name + "' gives back a '" + std::string(parts[2]) + "', which Godot has no type for here";
		return nullptr;
	}
	made->result = *result;
	made->any_number = parts.size() > 4 && parts[4] == "varargs";
	made->writes_text = made->any_number && (made->result == Variant::STRING || made->result == NOTHING);
	// str_to_var, bytes_to_var and type_convert read a value out of their argument, and null is one.
	made->reads_a_value = made->name.find("_to_var") != std::string::npos || made->name == "type_convert";
	for (size_t at = 0; parts[3] != "-" && at <= parts[3].size();) {
		const size_t comma = std::min(parts[3].find(',', at), parts[3].size());
		const std::string_view name = parts[3].substr(at, comma - at);
		const std::optional<int> type = type_of_name(name);
		if (!type) {
			*r_error = "Godot's '" + made->name + "' takes a '" + std::string(name) + "', which Godot has no type for here";
			return nullptr;
		}
		made->parameters.push_back(*type);
		at = comma + 1;
	}
	const StringName godot_name(String::utf8(made->name.c_str()));
	made->function = gdextension_interface::variant_get_ptr_utility_function(godot_name._native_ptr(),
			(GDExtensionInt)std::strtoll(std::string(parts[1]).c_str(), nullptr, 10));
	if (!made->function) {
		*r_error = "Godot has no global function '" + made->name + "' here";
		return nullptr;
	}
	const Utility *kept = made.get();
	utilities_.emplace(std::string_view(kept->signature), std::move(made));
	return kept;
}

bool GodotHost::call_utility(std::string_view p_signature, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_result,
		vm::Slot *r_result, std::string *r_error) {
	script_effect();
	const Utility *function = utility(p_signature, r_error);
	return function && call_utility(*function, p_arguments, p_count, p_result, r_result, r_error);
}

namespace {

// What one call of a global function holds: up to eight arguments sit on the stack, so that a call with
// that few allocates nothing.
template <typename T>
class Few {
	static constexpr size_t INLINE = 8;
	T inline_[INLINE];
	std::vector<T> more_;
	T *values_ = inline_;

public:
	explicit Few(size_t p_count) {
		if (p_count > INLINE) {
			more_.resize(p_count);
			values_ = more_.data();
		}
	}
	Few(const Few &) = delete;
	Few &operator=(const Few &) = delete;
	T &operator[](size_t p_index) { return values_[p_index]; }
	T *data() { return values_; }
};

} // namespace

bool GodotHost::call_utility(const Utility &p_function, const vm::Argument *p_arguments, size_t p_count, const vm::Wanted &p_result, vm::Slot *r_result,
		std::string *r_error) {
	script_effect();
	const Utility *function = &p_function;
	if (p_count < function->parameters.size() || (!function->any_number && p_count > function->parameters.size())) {
		const size_t wanted = function->parameters.size();
		*r_error = "Godot's '" + function->name + "' takes " + std::to_string(wanted) + (wanted == 1 ? " argument" : " arguments") + ", not " +
				std::to_string(p_count);
		return false;
	}
	// Each argument as a Variant first: that is what is checked, and what a function of any number takes.
	Few<Variant> values(p_count);
	Few<Bare> bare(p_count);
	Few<GDExtensionConstTypePtr> pointers(p_count);
	// Whether each bare value was made, to be let go.
	Few<uint8_t> made(p_count);
	bool ok = true;
	for (size_t i = 0; i < p_count; i++) {
		made[i] = 0;
	}
	for (size_t i = 0; i < p_count && ok; i++) {
		values[i] = function->writes_text ? text_argument(p_arguments[i]) : to_variant(p_arguments[i].kind, p_arguments[i].slot);
		const int type = function->any_number || i >= function->parameters.size() ? ANYTHING : function->parameters[i];
		if (!fits(values[i], type)) {
			*r_error = "Godot's '" + function->name + "' takes a " + type_name(type) + " for argument " + std::to_string(i + 1) + ", not a " +
					type_name((int)values[i].get_type());
			ok = false;
		} else if (type == ANYTHING) {
			pointers[i] = &values[i];
		} else {
			gdextension_interface::get_variant_to_type_constructor((GDExtensionVariantType)type)(bare[i].bytes, values[i]._native_ptr());
			pointers[i] = bare[i].bytes;
			made[i] = 1;
		}
	}
	Variant result;
	if (ok) {
		Bare returned{};
		const int type = function->result;
		function->function(type == ANYTHING ? (GDExtensionTypePtr)&result : (GDExtensionTypePtr)returned.bytes, pointers.data(), (int)p_count);
		if (type >= 0) {
			gdextension_interface::get_variant_from_type_constructor((GDExtensionVariantType)type)(result._native_ptr(), returned.bytes);
			if (GDExtensionPtrDestructor destroy = gdextension_interface::variant_get_ptr_destructor((GDExtensionVariantType)type)) {
				destroy(returned.bytes);
			}
		}
	}
	for (size_t i = 0; i < p_count; i++) {
		if (!made[i]) {
			continue;
		}
		if (GDExtensionPtrDestructor destroy = gdextension_interface::variant_get_ptr_destructor((GDExtensionVariantType)function->parameters[i])) {
			destroy(bare[i].bytes);
		}
	}
	if (ok && function->result == ANYTHING && result.get_type() == Variant::NIL && p_result.kind != ir::Kind::Void && !function->reads_a_value) {
		// Godot's own check of the arguments has nowhere to report through this entry: it gives back nothing.
		*r_error = "Godot's '" + function->name + "' could not work with the arguments it was given";
		return false;
	}
	return ok && take(p_result, result, function->name, r_result, r_error);
}

} // namespace kas3
