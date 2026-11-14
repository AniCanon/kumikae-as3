// Godot's global functions (lerpf, print, randf): called through the entry Godot gives extensions for
// them, which takes arguments of exact types and not boxed values. The interpreter passes the function's
// signature, written by the generator: "lerpf 998901048 float float,float,float", and "varargs" at the
// end for a function that takes any number of arguments.
#include "godot_host.h"

#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <cstdlib>
#include <vector>

using namespace godot;

namespace kas3 {

namespace {

constexpr int ANYTHING = -1; // A parameter or result declared as Variant.
constexpr int NOTHING = -2; // No result.

// A value of one of Godot's types as the entry takes it: the bare value, not a Variant. Large enough for
// every type a global function uses (the largest are the packed arrays, two words).
struct alignas(16) Bare {
	unsigned char bytes[64];
};

int type_of_name(std::string_view p_name) {
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
			return given == Variant::INT || (given == Variant::FLOAT && (double)p_value == (double)(int64_t)(double)p_value);
		case Variant::STRING:
			return given == Variant::STRING || given == Variant::STRING_NAME;
		case Variant::OBJECT:
			return given == Variant::OBJECT || given == Variant::NIL;
		default:
			return (int)given == p_type;
	}
}

std::string type_name(int p_type) {
	return p_type == ANYTHING ? "anything" : Variant::get_type_name((Variant::Type)p_type).utf8().get_data();
}

} // namespace

struct GodotHost::Utility {
	std::string signature; // The key: the text the interpreter passes.
	std::string name;
	GDExtensionPtrUtilityFunction function = nullptr;
	int result = NOTHING;
	std::vector<int> parameters;
	bool any_number = false;
	bool reads_a_value = false; // Null is a proper answer from it.
};

GodotHost::GodotHost() = default;
GodotHost::~GodotHost() = default;

const GodotHost::Utility *GodotHost::utility(std::string_view p_signature, std::string *r_error) {
	auto found = utilities_.find(p_signature);
	if (found != utilities_.end()) {
		return found->second.get();
	}
	// "name hash result parameters [varargs]".
	std::vector<std::string_view> parts;
	for (size_t at = 0; at <= p_signature.size();) {
		const size_t space = std::min(p_signature.find(' ', at), p_signature.size());
		parts.push_back(p_signature.substr(at, space - at));
		at = space + 1;
	}
	auto made = std::make_unique<Utility>();
	made->signature = std::string(p_signature);
	if (parts.size() < 4) {
		*r_error = "'" + made->signature + "' is not a global function of Godot's";
		return nullptr;
	}
	made->name = std::string(parts[0]);
	made->result = type_of_name(parts[2]);
	made->any_number = parts.size() > 4 && parts[4] == "varargs";
	// str_to_var, bytes_to_var and type_convert read a value out of their argument, and null is one.
	made->reads_a_value = made->name.find("_to_var") != std::string::npos || made->name == "type_convert";
	for (size_t at = 0; parts[3] != "-" && at <= parts[3].size();) {
		const size_t comma = std::min(parts[3].find(',', at), parts[3].size());
		made->parameters.push_back(type_of_name(parts[3].substr(at, comma - at)));
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
	const Utility *function = utility(p_signature, r_error);
	if (!function) {
		return false;
	}
	if (p_count < function->parameters.size() || (!function->any_number && p_count > function->parameters.size())) {
		*r_error = "Godot's '" + function->name + "' takes " + std::to_string(function->parameters.size()) + " arguments, not " +
				std::to_string(p_count);
		return false;
	}
	// Each argument as a Variant first: that is what is checked, and what a function of any number takes.
	std::vector<Variant> values(p_count);
	std::vector<Bare> bare(p_count);
	std::vector<GDExtensionConstTypePtr> pointers(p_count);
	std::vector<int> made; // The bare values that were made, to be let go.
	bool ok = true;
	for (size_t i = 0; i < p_count && ok; i++) {
		values[i] = to_variant(p_arguments[i].kind, p_arguments[i].slot);
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
			made.push_back((int)i);
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
	for (int index : made) {
		const int type = function->parameters[(size_t)index];
		if (GDExtensionPtrDestructor destroy = gdextension_interface::variant_get_ptr_destructor((GDExtensionVariantType)type)) {
			destroy(bare[(size_t)index].bytes);
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
