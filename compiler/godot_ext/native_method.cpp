#include "native_method.h"
#include "godot_calls.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace kas3 {

using godot::Array;
using godot::ClassDBSingleton;
using godot::Dictionary;
using godot::String;
using godot::StringName;
using godot::TypedArray;
using godot::Variant;

namespace {

bool subclass(std::string_view p_class, std::string_view p_base) {
	return p_class == p_base || ClassDBSingleton::get_singleton()->is_parent_class(to_godot(p_class), to_godot(p_base));
}

// A typed local can still contain null. The checked bind applies Godot's conversions
// and supplies its diagnostic for the parameter, just as in the interpreter.
bool checked_arguments(const NativeMethod &p_method, void *p_object, const void **p_arguments, uint32_t p_mismatched, Variant *r_result, std::string *r_error) {
	const auto &worked = p_method.worked;
	Values values;
	const Variant *args[MOST_OPERANDS];
	for (size_t i = 0; i < worked.parameters.size(); ++i) {
		const int type = worked.parameters[i];
		const void *arg = p_arguments[i];
		if (!arg) {
			args[i] = &values.next();
		} else if (p_mismatched & (uint32_t{ 1 } << i)) {
			args[i] = static_cast<const Variant *>(arg);
		} else {
			const auto make = godot::gdextension_interface::get_variant_from_type_constructor(static_cast<GDExtensionVariantType>(type));
			void *room = values.room();
			make(room, const_cast<void *>(arg));
			args[i] = static_cast<Variant *>(room);
		}
	}
	return checked_bound(worked, p_object, args, worked.parameters.size(), r_result, r_error);
}

// The backend passes math structs and scalars by address, text as an AS3 string, and other
// built-in values as borrowed handles. Only text conversions need storage during the call.
uint32_t convert_arguments(const GodotHost::Worked &p_worked, const void **p_arguments, uint32_t p_handles, Values &p_text) {
	uint32_t mismatched = 0;
	for (size_t i = 0; i < p_worked.parameters.size(); ++i) {
		const auto bit = uint32_t{ 1 } << i;
		if (!p_arguments[i]) {
			mismatched |= bit;
			continue;
		}
		const int type = p_worked.parameters[i];
		if (type == Variant::STRING || type == Variant::STRING_NAME || type == Variant::NODE_PATH) {
			const auto *text = static_cast<const vm::Str *>(p_arguments[i]);
			Variant &value = p_text.next();
			if (type == Variant::STRING_NAME) {
				value = name_for(p_worked.names[i], text);
			} else if (type == Variant::NODE_PATH) {
				value = godot::NodePath(to_godot(text->view()));
			} else {
				value = to_godot(text->view());
			}
			p_arguments[i] = inside(value, type);
		} else if (p_handles & bit) {
			auto *held = static_cast<Variant *>(const_cast<void *>(p_arguments[i]));
			if (held->get_type() == type) {
				p_arguments[i] = inside(*held, type);
			} else {
				mismatched |= bit;
			}
		}
	}
	return mismatched;
}

void raise(rt::Context *c, const std::string &p_message) {
	vm::Library library = c->host->library();
	vm::raise_engine(library, p_message);
}

} // namespace

std::optional<std::vector<Variant>> declared_defaults(const ir::EngineName &p_entry, const std::vector<int> &p_parameters, size_t p_given) {
	const StringName owner(String::utf8(p_entry.owner.c_str()));
	const String method = String::utf8(p_entry.name.c_str());
	const TypedArray<Dictionary> methods = ClassDBSingleton::get_singleton()->class_get_method_list(owner, true);
	for (int64_t i = 0; i < methods.size(); i++) {
		const Dictionary each = methods[i];
		if (String(each["name"]) != method) {
			continue;
		}
		const Array declared = each["default_args"];
		std::vector<Variant> values;
		for (size_t parameter = p_given; parameter < p_parameters.size(); parameter++) {
			const int64_t at = static_cast<int64_t>(parameter) - (static_cast<int64_t>(p_parameters.size()) - declared.size());
			if (at < 0 || at >= declared.size()) {
				return std::nullopt;
			}
			const Variant value = declared[at];
			const int type = value.get_type();
			if (type != p_parameters[parameter] || type == Variant::OBJECT || type == Variant::STRING || type == Variant::STRING_NAME ||
					type == Variant::NODE_PATH) {
				return std::nullopt;
			}
			values.push_back(value);
		}
		return values;
	}
	return std::nullopt;
}

std::unique_ptr<NativeMethod> prepare_native_method(const ir::EngineName &p_name, const ir::Function &p_function,
		const ir::Instruction &p_in, const GodotHost::Worked &p_worked, std::vector<Variant> p_defaults) {
	if (p_in.op != ir::Op::HostCall || p_worked.what != GodotHost::Worked::What::Bound) {
		return nullptr;
	}
	const auto &list = p_function.argument_lists[p_in.c];
	const auto words = godot_words(p_name.value);
	if (words.size() != 5 || list.empty() || list.size() > p_worked.parameters.size() + 1 || p_function.registers[list[0]] != ir::Kind::Ref ||
			p_worked.parameters.size() > MOST_OPERANDS || std::find(p_worked.parameters.begin(), p_worked.parameters.end(), ANY_VALUE) != p_worked.parameters.end()) {
		return nullptr;
	}
	const auto types = words[4];
	for (size_t i = 1, at = 0; i < list.size(); ++i) {
		const size_t comma = std::min(types.find(',', at), types.size());
		if (p_function.registers[list[i]] == ir::Kind::Ref && (p_worked.parameters[i - 1] != Variant::OBJECT ||
				list[i] >= p_function.engine_types.size() || !subclass(p_function.engine_types[list[i]], types.substr(at, comma - at)))) {
			return nullptr;
		}
		at = comma + 1;
	}
	vm::Wanted wanted;
	if (p_in.a != ir::NONE) {
		wanted.kind = p_function.registers[p_in.a];
		if (p_in.a < p_function.engine_types.size() && p_function.engine_types[p_in.a] != ir::ANY_GODOT_TYPE) {
			wanted.type = p_function.engine_types[p_in.a];
		}
	}
	const size_t given = list.size() - 1;
	if (given + p_defaults.size() != p_worked.parameters.size() || p_defaults.size() > rt::MOST_DEFAULTS) {
		return nullptr;
	}
	auto method = std::make_unique<NativeMethod>(NativeMethod{ p_worked, wanted, p_worked.result == Variant::OBJECT && subclass(words[3], "RefCounted"), given,
			std::move(p_defaults) });
	for (size_t i = 0; i < method->defaults.size(); i++) {
		method->default_at[i] = inside(method->defaults[i], method->defaults[i].get_type());
	}
	return method;
}

void call_method_bind(const void *p_bind, void *p_object, const void *const *p_arguments, void *r_result) {
	godot::gdextension_interface::object_method_bind_ptrcall(p_bind, p_object, p_arguments, r_result);
}

void call_native_method(rt::Context *c, const rt::Resolved &p_use, rt::Ref p_receiver, void *p_object,
		const void **p_arguments, uint32_t p_handles, vm::Slot *r_result, void *r_parts) {
	const auto &method = *static_cast<const NativeMethod *>(p_use.method);
	const auto &worked = method.worked;
	if (!p_object) {
		if (p_receiver.handle) {
			raise(c, FREED);
		} else {
			c->host->library().raise_null();
		}
		return;
	}
	// The parameters the call leaves out are Godot's declared defaults, made when the place was prepared.
	for (size_t i = method.given; i < worked.parameters.size(); i++) {
		p_arguments[i] = method.default_at[i - method.given];
	}
	Values text;
	const uint32_t mismatched = convert_arguments(worked, p_arguments, p_handles, text);
	auto ptrcall = [&](void *out) { call_method_bind(worked.bound, p_object, p_arguments, out); };
	Variant result;
	std::string error;
	if (mismatched) {
		if (!checked_arguments(method, p_object, p_arguments, mismatched, &result, &error)) {
			raise(c, error);
			return;
		}
		if (r_parts) {
			const auto copy = godot::gdextension_interface::get_variant_to_type_constructor(static_cast<GDExtensionVariantType>(worked.result));
			copy(r_parts, result._native_ptr());
			return;
		}
	} else if (r_parts || worked.result == NO_VALUE) {
		ptrcall(r_parts);
		return;
	} else if (worked.result == Variant::OBJECT) {
		void *object = nullptr;
		ptrcall(&object);
		if (object) {
			result = godot::internal::get_object_instance_binding(object);
			// A Ref<T> result conveys an owning reference through the ABI. The Variant now owns one.
			if (method.returns_reference) {
				godot::gdextension_interface::ref_set_object(&object, nullptr);
			}
		}
	} else if (worked.result == ANY_VALUE) {
		ptrcall(result._native_ptr());
	} else {
		result = worked.zero;
		ptrcall(inside(result, worked.result));
	}
	if (!take(method.result, result, worked.name, r_result, &error)) {
		raise(c, error);
	}
}

} // namespace kas3
