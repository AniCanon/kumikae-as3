#include "members.h"

#include "any.h"
#include "godot_host.h"
#include "program.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <godot_cpp/core/gdextension_interface_loader.hpp>

#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace godot;

namespace kas3 {

namespace {

std::string_view text_of(const CharString &p_utf8) {
	return std::string_view(p_utf8.get_data(), (size_t)p_utf8.length());
}

// The name a method has in AS3: its function's name after the class ("Player/jump").
std::string_view own_name(const ir::Function &p_function) {
	const size_t slash = p_function.name.rfind('/');
	return std::string_view(p_function.name).substr(slash == std::string::npos ? 0 : slash + 1);
}

bool is_signal(const ir::Module &p_module, uint32_t p_class, std::string_view p_field) {
	for (uint32_t at = p_class; at < p_module.classes.size(); at = p_module.classes[at].base) {
		for (const std::string &name : p_module.classes[at].signals) {
			if (name == p_field) {
				return true;
			}
		}
	}
	return false;
}

bool inherits(const std::string &p_class, const char *p_base) {
	return ClassDBSingleton::get_singleton()->is_parent_class(StringName(p_class.c_str()), StringName(p_base));
}

// One of Godot's classes, as a property's class, and the hint that names it for a Resource or a Node.
void object_hint(const std::string &p_engine_type, String *r_class_name, int *r_hint, String *r_hint_string) {
	*r_class_name = String::utf8(p_engine_type.c_str());
	if (!p_engine_type.empty() && inherits(p_engine_type, "Resource")) {
		*r_hint = PROPERTY_HINT_RESOURCE_TYPE;
		*r_hint_string = *r_class_name;
	} else if (!p_engine_type.empty() && inherits(p_engine_type, "Node")) {
		*r_hint = PROPERTY_HINT_NODE_TYPE;
		*r_hint_string = *r_class_name;
	}
}

// The type of a script object's property: an Array is Godot's Array, a Dictionary Godot's Dictionary; a
// script object is its Godot object, when it has one. NIL for any other.
int script_object_type(const ir::Module &p_module, uint32_t p_class, String *r_class_name) {
	if (p_class < p_module.classes.size() && p_module.classes[p_class].array) {
		return Variant::ARRAY;
	}
	if (p_class < p_module.classes.size() && p_module.classes[p_class].dictionary) {
		return Variant::DICTIONARY;
	}
	if (p_class < p_module.classes.size() && !p_module.classes[p_class].native_base.empty()) {
		*r_class_name = String::utf8(p_module.classes[p_class].native_base.c_str());
		return Variant::OBJECT;
	}
	return Variant::NIL;
}

// How Godot describes a value of this kind: its type, class, hint and usage.
Dictionary describe(const ir::Module &p_module, std::string_view p_name, ir::Kind p_kind, const std::string &p_engine_type, uint32_t p_class,
		bool p_exported) {
	int type = Variant::NIL;
	String class_name;
	int hint = PROPERTY_HINT_NONE;
	String hint_string;
	int usage = p_exported ? PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_SCRIPT_VARIABLE : PROPERTY_USAGE_SCRIPT_VARIABLE;
	switch (p_kind) {
		case ir::Kind::F64:
			type = Variant::FLOAT;
			break;
		case ir::Kind::I32:
		case ir::Kind::U32:
		case ir::Kind::I64:
			type = Variant::INT;
			break;
		case ir::Kind::Bool:
			type = Variant::BOOL;
			break;
		case ir::Kind::Str:
			type = Variant::STRING;
			break;
		case ir::Kind::Val:
			type = value_type(p_engine_type).value_or(-1);
			break;
		case ir::Kind::Ref:
			type = Variant::OBJECT;
			object_hint(p_engine_type, &class_name, &hint, &hint_string);
			break;
		case ir::Kind::Obj:
			type = script_object_type(p_module, p_class, &class_name);
			break;
		default:
			type = -1; // Any value.
			break;
	}
	if (type < 0) {
		type = Variant::NIL;
		usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
	}
	Dictionary property;
	property["name"] = String::utf8(p_name.data(), (int64_t)p_name.size());
	property["type"] = type;
	property["class_name"] = StringName(class_name);
	property["hint"] = hint;
	property["hint_string"] = hint_string;
	property["usage"] = usage;
	return property;
}

Dictionary method_info(const ir::Module &p_module, const ir::Function &p_function, std::string_view p_name) {
	Array arguments;
	const uint32_t first = p_function.is_static ? 0 : 1;
	const uint32_t fixed = p_function.rest ? p_function.parameters - 1 : p_function.parameters;
	for (uint32_t reg = first; reg < fixed; reg++) {
		std::string_view name;
		for (const ir::Local &local : p_function.locals) {
			name = local.reg == reg ? std::string_view(local.name) : name;
		}
		const std::string &engine = reg < p_function.engine_types.size() ? p_function.engine_types[reg] : std::string();
		const uint32_t klass = reg < p_function.parameter_classes.size() ? p_function.parameter_classes[reg] : ir::NONE;
		Dictionary argument = describe(p_module, name, p_function.registers[reg], engine, klass, false);
		argument["usage"] = PROPERTY_USAGE_DEFAULT | ((int)argument["usage"] & PROPERTY_USAGE_NIL_IS_VARIANT);
		arguments.push_back(argument);
	}
	// What a default is, is worked out when the method runs: Godot is told how many there are.
	Array defaults;
	for (uint32_t i = p_function.required; i < fixed; i++) {
		defaults.push_back(Variant());
	}
	Dictionary returned = p_function.result == ir::Kind::Void ? describe(p_module, "", ir::Kind::Void, "", ir::NONE, false)
															   : describe(p_module, "", p_function.result, "", ir::NONE, false);
	if (p_function.result == ir::Kind::Void) {
		returned["type"] = Variant::NIL;
		returned["usage"] = PROPERTY_USAGE_DEFAULT;
	} else {
		returned["usage"] = PROPERTY_USAGE_DEFAULT | ((int)returned["usage"] & PROPERTY_USAGE_NIL_IS_VARIANT);
	}
	Dictionary method;
	method["name"] = String::utf8(p_name.data(), (int64_t)p_name.size());
	method["args"] = arguments;
	method["default_args"] = defaults;
	method["return"] = returned;
	method["flags"] = p_function.rest || p_function.takes_extra ? METHOD_FLAG_NORMAL | METHOD_FLAG_VARARG : METHOD_FLAG_NORMAL;
	method["id"] = 0;
	return method;
}

// One of Godot's values made from numbers, as `new` makes it: a whole number goes as an integer.
bool made_value(const ir::Constant &p_first, Variant *r_value) {
	const std::optional<int> type = value_type(p_first.text);
	if (!type) {
		return false;
	}
	std::vector<Variant> parts;
	for (double part : p_first.parts) {
		parts.push_back(std::trunc(part) == part && std::fabs(part) < 9007199254740992.0 ? Variant((int64_t)part) : Variant(part));
	}
	std::vector<GDExtensionConstVariantPtr> pointers;
	for (const Variant &part : parts) {
		pointers.push_back(part._native_ptr());
	}
	GDExtensionCallError error;
	error.error = GDEXTENSION_CALL_OK;
	Variant made;
	gdextension_interface::variant_construct((GDExtensionVariantType)*type, made._native_ptr(), pointers.data(), (int32_t)pointers.size(), &error);
	*r_value = made;
	return error.error == GDEXTENSION_CALL_OK;
}

} // namespace

namespace {

// The public method of the class of that name, or NONE.
uint32_t public_method(Project &p_program, uint32_t p_class, std::string_view p_name) {
	const uint32_t method = p_program.interpreter().find_method(p_class, p_name);
	return method != ir::NONE && p_program.module().functions[method].is_public ? method : ir::NONE;
}

Reach look_up(Project &p_program, uint32_t p_class, const StringName &p_name) {
	Reach found;
	const ir::Class &klass = p_program.module().classes[p_class];
	const CharString utf8 = String(p_name).utf8();
	const std::string_view name = text_of(utf8);
	found.getter = public_method(p_program, p_class, "get " + std::string(name));
	found.setter = public_method(p_program, p_class, "set " + std::string(name));
	for (const auto &[godot, function] : klass.virtuals) {
		if (godot == name) {
			found.member = Member{ Member::What::Method, function };
			return found;
		}
	}
	if (const uint32_t method = public_method(p_program, p_class, name); method != ir::NONE) {
		found.member = Member{ Member::What::Method, method };
	} else if (const std::optional<uint32_t> field = klass.field(name, true)) {
		found.member = Member{ Member::What::Field, *field };
	}
	return found;
}

const void *interned(const StringName &p_name) {
	return *reinterpret_cast<const void *const *>(p_name._native_ptr());
}

// What a class's names reach, by the name's interned text, which equal names share. Only names that reach
// something are listed, all of them at once: Godot asks about any name set or read on the object
// (`position`, or whatever a game makes up), and a name that is not listed reaches nothing, so what is kept
// is bounded by the class. Each keeps its name, so that the text is not freed and taken by another name
// while the answer stands.
struct Answers {
	bool listed = false;
	std::unordered_map<const void *, std::pair<StringName, Reach>> reached;
};

// reach()'s answers for one program.
struct Reached {
	uint64_t generation = UINT64_MAX;
	std::vector<Answers> by_class;
};

Reached &reached() {
	static Reached all;
	return all;
}

// Every name of the class that reaches something: its public fields, its public methods (a getter or a
// setter by the name it gives), and Godot's virtuals it answers. look_up() says what each reaches.
void list_reached(Project &p_program, uint32_t p_class, Answers *r_answers) {
	const ir::Module &module = p_program.module();
	const ir::Class &klass = module.classes[p_class];
	std::unordered_set<std::string_view> names;
	for (const ir::Field &field : klass.fields) {
		names.insert(field.name);
	}
	for (const uint32_t method : klass.methods) {
		std::string_view name = own_name(module.functions[method]);
		if (name.size() > 4 && (name.substr(0, 4) == "get " || name.substr(0, 4) == "set ")) {
			name.remove_prefix(4);
		}
		names.insert(name);
	}
	for (const auto &[godot, function] : klass.virtuals) {
		names.insert(godot);
	}
	for (const std::string_view name : names) {
		const StringName godot_name(String::utf8(name.data(), (int64_t)name.size()));
		const Reach answer = look_up(p_program, p_class, godot_name);
		if (answer.member.what != Member::What::None || answer.getter != ir::NONE || answer.setter != ir::NONE) {
			r_answers->reached.emplace(interned(godot_name), std::make_pair(godot_name, answer));
		}
	}
	r_answers->listed = true;
}

} // namespace

Reach reach(uint32_t p_class, const StringName &p_name) {
	Project &program = Project::get();
	if (!program.valid() || p_class >= program.module().classes.size()) {
		return Reach{};
	}
	if (!on_main_thread()) {
		return look_up(program, p_class, p_name); // Refused by the caller; the answers are the main thread's.
	}
	Reached &all = reached();
	if (all.generation != Project::generation()) {
		all.by_class.assign(program.module().classes.size(), {});
		all.generation = Project::generation();
	}
	Answers &answers = all.by_class[p_class];
	if (!answers.listed) {
		list_reached(program, p_class, &answers);
	}
	const auto found = answers.reached.find(interned(p_name));
	return found == answers.reached.end() ? Reach{} : found->second.second;
}

void forget_reached() {
	reached() = Reached{};
}

Member find_member(uint32_t p_class, const StringName &p_name) {
	return reach(p_class, p_name).member;
}

Variant read_field(const vm::Object *p_object, uint32_t p_field) {
	const ir::Class &klass = Project::get().module().classes[p_object->klass];
	const ir::Field &field = klass.fields[p_field];
	const vm::Slot value = vm::field(klass, const_cast<vm::Object *>(p_object), p_field);
	const vm::Object *held = field.kind == ir::Kind::Obj ? static_cast<const vm::Object *>(value.p)
			: field.kind == ir::Kind::Any && vm::any::tag(value) == vm::AnyTag::Object ? static_cast<const vm::Object *>(vm::any::pointer(value))
																						 : nullptr;
	if (!held && !(field.kind == ir::Kind::Obj && !value.p)) {
		return to_variant(field.kind, value);
	}
	Variant owner;
	std::string why;
	if (held && !object_element(held, &owner, &why)) {
		const std::string &held_class = Project::get().module().classes[held->klass].name;
		UtilityFunctions::push_error(String("ActionScript: the field '") + String::utf8(field.name.c_str()) + "' holds an object of '" +
				String::utf8(held_class.c_str()) + "', a class that extends none of Godot's, which Godot can't hold. Godot gets null.");
	}
	return owner;
}

bool write_field(vm::Object *p_object, uint32_t p_field, const Variant &p_value) {
	Project &program = Project::get();
	const ir::Module &module = program.module();
	const ir::Field &field = module.classes[p_object->klass].fields[p_field];
	if (!field.is_public || field.constant || is_signal(module, p_object->klass, field.name)) {
		return false;
	}
	vm::Slot taken;
	taken.bits = 0;
	if (field.kind == ir::Kind::Obj) {
		// Only a container comes from Godot, copied: a script object or a Date can't.
		const Variant::Type type = container_type(field.klass);
		if (type == Variant::NIL || (p_value.get_type() != type && p_value.get_type() != Variant::NIL)) {
			return false;
		}
		const std::optional<vm::Slot> copied = container_from_variant(field.klass, p_value);
		if (!copied) {
			return false;
		}
		taken = *copied;
	} else {
		if (!arrives(field.kind, field.engine_type, p_value)) {
			return false;
		}
		const std::optional<vm::Slot> made = from_variant(field.kind, p_value);
		if (!made) {
			UtilityFunctions::push_error(String("Error: ") + String::utf8(vm::OUT_OF_MEMORY.data(), (int64_t)vm::OUT_OF_MEMORY.size()));
			return false;
		}
		taken = *made;
	}
	program.interpreter().set_field(p_object, p_field, taken);
	return true;
}

Variant::Type container_type(uint32_t p_class) {
	const ir::Module &module = Project::get().module();
	const ir::Class *klass = p_class < module.classes.size() ? &module.classes[p_class] : nullptr;
	return klass && klass->array ? Variant::ARRAY : klass && klass->dictionary ? Variant::DICTIONARY : Variant::NIL;
}

std::optional<vm::Slot> container_from_variant(uint32_t p_class, const Variant &p_value) {
	vm::Slot taken;
	taken.bits = 0;
	if (p_value.get_type() == Variant::NIL) {
		return taken;
	}
	Variant held = p_value;
	vm::Interpreter &interpreter = Project::get().interpreter();
	taken.p = container_type(p_class) == Variant::ARRAY ? static_cast<void *>(interpreter.copy_engine_array(&held, p_class))
														: static_cast<void *>(interpreter.copy_engine_dictionary(&held, p_class));
	return taken.p ? std::optional<vm::Slot>(taken) : std::nullopt;
}

TypedArray<Dictionary> property_list(uint32_t p_class) {
	TypedArray<Dictionary> list;
	Project &program = Project::get();
	if (!program.valid() || p_class >= program.module().classes.size()) {
		return list;
	}
	const ir::Module &module = program.module();
	const ir::Class &klass = module.classes[p_class];
	for (size_t i = 0; i < klass.fields.size(); i++) {
		const ir::Field &field = klass.fields[i];
		const StringName name(String::utf8(field.name.c_str()));
		// One entry for each name Godot can reach: the field find_member() would give.
		const Member member = find_member(p_class, name);
		if (member.what == Member::What::Field && member.index == i && !is_signal(module, p_class, field.name)) {
			list.push_back(describe(module, field.name, field.kind, field.engine_type, field.klass, field.exported));
		}
	}
	// A public getter or setter, which GDScript reaches as a property (instance.cpp): listed once, with its
	// setter's parameter's type, or its getter's result's. A field or method of the name comes first.
	std::unordered_set<std::string_view> accessors;
	for (uint32_t function : klass.methods) {
		const ir::Function &method = module.functions[function];
		const std::string_view own = own_name(method);
		const std::string_view name = own.substr(own.size() > 4 ? 4 : own.size());
		if (!method.is_public || (own.substr(0, 4) != "get " && own.substr(0, 4) != "set ") || program.interpreter().find_method(p_class, own) != function ||
				find_member(p_class, StringName(String::utf8(name.data(), (int64_t)name.size()))).what != Member::What::None ||
				!accessors.insert(name).second) {
			continue;
		}
		const uint32_t write = program.interpreter().find_method(p_class, "set " + std::string(name));
		const ir::Function *setter = write != ir::NONE && module.functions[write].is_public ? &module.functions[write] : nullptr;
		if (setter && setter->registers.size() > 1) {
			const std::string &engine = setter->engine_types.size() > 1 ? setter->engine_types[1] : std::string();
			const uint32_t holds = setter->parameter_classes.size() > 1 ? setter->parameter_classes[1] : ir::NONE;
			list.push_back(describe(module, name, setter->registers[1], engine, holds, false));
		} else {
			const uint32_t read = program.interpreter().find_method(p_class, "get " + std::string(name));
			list.push_back(describe(module, name, module.functions[read].result, "", ir::NONE, false));
		}
	}
	return list;
}

TypedArray<Dictionary> method_list(uint32_t p_class) {
	TypedArray<Dictionary> list;
	Project &program = Project::get();
	if (!program.valid() || p_class >= program.module().classes.size()) {
		return list;
	}
	const ir::Module &module = program.module();
	const ir::Class &klass = module.classes[p_class];
	std::unordered_set<uint32_t> virtuals;
	for (const auto &[godot, function] : klass.virtuals) {
		virtuals.insert(function);
		list.push_back(method_info(module, module.functions[function], godot));
	}
	std::unordered_set<std::string_view> seen;
	for (uint32_t function : klass.methods) {
		const ir::Function &method = module.functions[function];
		const std::string_view name = own_name(method);
		const bool accessor = name.substr(0, 4) == "get " || name.substr(0, 4) == "set "; // A property: property_list().
		if (method.is_public && !accessor && !virtuals.count(function) && seen.insert(name).second &&
				program.interpreter().find_method(p_class, name) == function) {
			list.push_back(method_info(module, method, name));
		}
	}
	return list;
}

bool field_default(uint32_t p_class, const StringName &p_name, Variant *r_value) {
	const Member member = find_member(p_class, p_name);
	if (member.what != Member::What::Field) {
		return false;
	}
	const ir::Field &field = Project::get().module().classes[p_class].fields[member.index];
	const ir::Constant &first = field.first;
	if (!field.exported || first.form == ir::Constant::Form::None) {
		return false;
	}
	vm::Slot slot = vm::empty_value(field.kind == ir::Kind::F64, field.starts_null);
	switch (first.form) {
		case ir::Constant::Form::Empty:
			break;
		case ir::Constant::Form::Value:
			return made_value(first, r_value);
		case ir::Constant::Form::Number:
			slot.bits = 0;
			if (field.kind == ir::Kind::F64) {
				slot.d = first.number;
			} else if (field.kind == ir::Kind::I32) {
				slot.i = vm::to_int32(first.number);
			} else if (field.kind == ir::Kind::U32) {
				slot.u = vm::to_uint32(first.number);
			} else if (field.kind == ir::Kind::I64 && std::isfinite(first.number)) {
				slot.l = (int64_t)first.number;
			} else {
				return false;
			}
			break;
		case ir::Constant::Form::Boolean:
			*r_value = first.number != 0;
			return true;
		case ir::Constant::Form::Text:
			*r_value = String::utf8(first.text.c_str(), (int64_t)first.text.size());
			return true;
		default:
			*r_value = Variant();
			return true;
	}
	*r_value = to_variant(field.kind, slot);
	return true;
}

} // namespace kas3
