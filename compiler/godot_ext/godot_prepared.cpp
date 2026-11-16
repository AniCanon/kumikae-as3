// The engine instructions, worked out once when a program is loaded (vm::EngineHandles::prepare) and run
// with what Godot gave for them: an operator's evaluator for its operands' types, a value type's method and
// the getter and setter of its parts called on the bare values, a class's method or property accessor by
// its method bind, a constant's value, a global function's entry. Nothing is looked up by name, and no text
// is made, as one runs. One that meets values of other types than it was worked out for (a null, a value
// held untyped) is left to the requests by name (godot_host.cpp), which say what is wrong.
#include "godot_calls.h"
#include "handles.h"
#include "worked.h"

#include "godot_names.h"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>

using namespace godot;

namespace kas3 {

namespace {

using What = GodotHost::Worked::What;

// The most operands a worked-out call takes on the stack; one with more goes by name.
constexpr size_t MOST = 16;

// A type as the name table writes it ("float", "Vector2", "enum::Node.ProcessMode", "Node"), as a Variant
// type, ANY_VALUE or NO_VALUE.
int type_named(std::string_view p_name) {
	if (p_name == "Variant") {
		return ANY_VALUE;
	}
	if (p_name == "void") {
		return NO_VALUE;
	}
	if (p_name.rfind("enum::", 0) == 0 || p_name.rfind("bitfield::", 0) == 0) {
		return Variant::INT;
	}
	if (p_name.rfind("typedarray::", 0) == 0) {
		return Variant::ARRAY;
	}
	if (p_name.rfind("typeddictionary::", 0) == 0) {
		return Variant::DICTIONARY;
	}
	const std::optional<int> type = value_type(p_name);
	return type ? *type : (int)Variant::OBJECT; // Any other name is one of Godot's classes.
}

// The type of what a register holds, as Godot's evaluators take it; nothing for one that may hold more
// than one type (an untyped value, any of Godot's values).
std::optional<int> type_held(const ir::Function &p_function, uint32_t p_register) {
	switch (p_function.registers[p_register]) {
		case ir::Kind::F64:
			return (int)Variant::FLOAT;
		case ir::Kind::I32:
		case ir::Kind::U32:
		case ir::Kind::I64:
			return (int)Variant::INT;
		case ir::Kind::Bool:
			return (int)Variant::BOOL;
		case ir::Kind::Val: {
			const std::string_view godot = p_register < p_function.engine_types.size() ? std::string_view(p_function.engine_types[p_register]) : "";
			return value_type(godot);
		}
		default:
			return std::nullopt;
	}
}

// The words of a list of types ("StringName,bool"), each as a type; "-" for none.
std::vector<int> types_named(std::string_view p_list) {
	std::vector<int> types;
	for (size_t at = 0; p_list != "-" && at <= p_list.size();) {
		const size_t comma = std::min(p_list.find(',', at), p_list.size());
		types.push_back(type_named(p_list.substr(at, comma - at)));
		at = comma + 1;
	}
	return types;
}

Variant zero_of(int p_type) {
	Variant zero;
	if (p_type >= 0) {
		GDExtensionCallError error;
		gdextension_interface::variant_construct((GDExtensionVariantType)p_type, zero._native_ptr(), nullptr, 0, &error);
	}
	return zero;
}

// Where a Variant of a type keeps its bare value, as Godot's entries read and write it.
void *inside(Variant &p_value, int p_type) {
	static GDExtensionVariantGetInternalPtrFunc getters[Variant::VARIANT_MAX] = {};
	if (!getters[p_type]) {
		getters[p_type] = gdextension_interface::variant_get_ptr_internal_getter((GDExtensionVariantType)p_type);
	}
	return getters[p_type](p_value._native_ptr());
}

// A number or a Boolean as an entry takes it bare.
union Scalar {
	double number;
	int64_t integer;
	GDExtensionBool boolean;
};

// Where an operand's bare value of a type is: in `r_scalar` for a number or a Boolean, inside the handle
// for one of Godot's values. Null if the operand does not hold that type (a null, a whole number where a
// Number is not taken as one): the instruction then goes by name.
const void *bare(const vm::Argument &p_operand, int p_type, Scalar *r_scalar) {
	const ir::Kind kind = p_operand.kind;
	switch (p_type) {
		case Variant::FLOAT:
			r_scalar->number = kind == ir::Kind::F64 ? p_operand.slot.d : kind == ir::Kind::I32 ? p_operand.slot.i : kind == ir::Kind::U32 ? p_operand.slot.u : 0.0;
			return kind == ir::Kind::F64 || kind == ir::Kind::I32 || kind == ir::Kind::U32 ? r_scalar : nullptr;
		case Variant::INT:
			r_scalar->integer = kind == ir::Kind::I64 ? p_operand.slot.l : kind == ir::Kind::I32 ? p_operand.slot.i : (int64_t)p_operand.slot.u;
			return kind == ir::Kind::I64 || kind == ir::Kind::I32 || kind == ir::Kind::U32 ? r_scalar : nullptr;
		case Variant::BOOL:
			r_scalar->boolean = p_operand.slot.b;
			return kind == ir::Kind::Bool ? r_scalar : nullptr;
		default: {
			Variant *held = kind == ir::Kind::Val ? static_cast<Variant *>(p_operand.slot.p) : nullptr;
			return held && held->get_type() == p_type ? inside(*held, p_type) : nullptr;
		}
	}
}

// The object behind a handle, as Godot's method binds take it: null if Godot has freed it. False if the
// handle holds no object at all (null), which the requests by name report.
bool object_of(const vm::Argument &p_operand, GDExtensionObjectPtr *r_object) {
	const Variant *held = static_cast<const Variant *>(p_operand.slot.p);
	if (p_operand.kind != ir::Kind::Ref || !held || held->get_type() != Variant::OBJECT) {
		return false;
	}
	*r_object = gdextension_interface::object_get_instance_from_id(gdextension_interface::variant_get_object_instance_id(held->_native_ptr()));
	return true;
}

// A String given where a StringName is taken: the StringName made the last time this text was given, or
// made now and kept.
const Variant &name_for(NameKept &p_kept, const vm::Str *p_text) {
	const std::string_view text = p_text->view();
	if (p_kept.name.get_type() != Variant::STRING_NAME || p_kept.text != text) {
		p_kept.text.assign(text.data(), text.size());
		p_kept.name = StringName(String::utf8(text.data(), (int64_t)text.size()));
	}
	return p_kept.name;
}

// An argument as a Variant for a parameter of this type: a StringName from text once, else the value as is.
void argument(const GodotHost::Worked &p_worked, size_t p_parameter, const vm::Argument &p_value, Variant *r_value) {
	if (p_parameter < p_worked.parameters.size() && p_worked.parameters[p_parameter] == Variant::STRING_NAME && p_value.kind == ir::Kind::Str &&
			p_value.slot.p) {
		*r_value = name_for(p_worked.names[p_parameter], static_cast<const vm::Str *>(p_value.slot.p));
		return;
	}
	*r_value = to_variant(p_value.kind, p_value.slot);
}

// The Variants one call holds, made only as they are needed: a Variant is made and let go through Godot,
// so sixteen of them for a call of one argument would cost more than the call.
class Values {
	alignas(Variant) unsigned char memory_[MOST][sizeof(Variant)];
	size_t made_ = 0;

public:
	Values() = default;
	Values(const Values &) = delete;
	Values &operator=(const Values &) = delete;
	~Values() {
		for (size_t i = 0; i < made_; i++) {
			reinterpret_cast<Variant *>(memory_[i])->~Variant();
		}
	}
	Variant &next() { return *new (memory_[made_++]) Variant(); }
};

// The handle to make a result of one of Godot's value types in: the one the result's register holds, if it
// holds that type (each handle is its register's own), or a new one.
Variant *result_handle(const vm::Wanted &p_result, const GodotHost::Worked &p_worked) {
	Variant *held = static_cast<Variant *>(p_result.reuse);
	return held && held->get_type() == p_worked.result ? held : make_handle(p_worked.zero);
}

// Whether a result goes straight into a handle: one of Godot's value types where the register holds that.
bool into_handle(const vm::Wanted &p_result, int p_type) {
	return p_result.kind == ir::Kind::Val && p_type > Variant::STRING && p_type < Variant::OBJECT;
}

// Whether a type is one a method bind takes and gives bare where a Variant would hold it inline: a number,
// a Boolean, or a value type from Vector2 to Color.
bool held_bare(int p_type) {
	return p_type == Variant::BOOL || p_type == Variant::INT || p_type == Variant::FLOAT || (p_type >= Variant::VECTOR2 && p_type <= Variant::COLOR);
}

// A method bind called on the bare values (Worked::bare), when every argument is given and the register the
// result goes to is of the kind it gives: true, or nothing to call it with Variants. Godot checks nothing
// here; what it takes is what the name table says it takes, and the arguments are of those types.
std::optional<bool> run_bare(const GodotHost::Worked &p_worked, GDExtensionObjectPtr p_object, const vm::Argument *p_arguments, size_t p_count,
		const vm::Wanted &p_result, vm::Slot *r_result) {
	const size_t first = p_worked.index >= 0 ? 1 : 0;
	if (p_count + first != p_worked.parameters.size()) {
		return std::nullopt;
	}
	Scalar scalars[MOST];
	GDExtensionConstTypePtr pointers[MOST];
	if (first) {
		scalars[0].integer = p_worked.index;
		pointers[0] = &scalars[0];
	}
	for (size_t i = 0; i < p_count; i++) {
		if (!(pointers[first + i] = bare(p_arguments[i], p_worked.parameters[first + i], &scalars[first + i]))) {
			return std::nullopt;
		}
	}
	const int type = p_worked.result;
	const ir::Kind kind = p_result.kind;
	Scalar out;
	out.integer = 0;
	if (type == NO_VALUE) {
		gdextension_interface::object_method_bind_ptrcall(p_worked.bound, p_object, pointers, nullptr);
		return true;
	}
	if (!r_result) {
		return std::nullopt;
	}
	if ((type == Variant::BOOL && kind == ir::Kind::Bool) || (type == Variant::FLOAT && kind == ir::Kind::F64) || (type == Variant::INT && kind == ir::Kind::I64)) {
		gdextension_interface::object_method_bind_ptrcall(p_worked.bound, p_object, pointers, &out);
		if (type == Variant::BOOL) {
			r_result->b = out.boolean != 0;
		} else if (type == Variant::FLOAT) {
			r_result->d = out.number;
		} else {
			r_result->l = out.integer;
		}
		return true;
	}
	if (!into_handle(p_result, type)) {
		return std::nullopt;
	}
	Variant *made = result_handle(p_result, p_worked);
	gdextension_interface::object_method_bind_ptrcall(p_worked.bound, p_object, pointers, inside(*made, type));
	r_result->p = made;
	return true;
}

} // namespace

// A Variant's internal pointer is taken for a handle's own value: a method that changes its value (append)
// changes the handle, as a call by name on it does.
bool GodotHost::run_operator(const Worked &p_worked, const vm::Argument *p_operands, size_t p_count, const vm::Wanted &p_result, vm::Slot *r_result) {
	Scalar left_scalar, right_scalar;
	const void *left = bare(p_operands[0], p_worked.operands[0], &left_scalar);
	const void *right = p_count == 2 ? bare(p_operands[1], p_worked.operands[1], &right_scalar) : nullptr;
	if (!left || (p_count == 2 && !right) || !r_result) {
		return false;
	}
	Scalar out;
	out.integer = 0;
	switch (p_worked.result) {
		case Variant::BOOL:
			p_worked.evaluator(left, right, &out.boolean);
			r_result->b = out.boolean != 0;
			return true;
		case Variant::FLOAT:
			p_worked.evaluator(left, right, &out.number);
			r_result->d = out.number;
			return true;
		case Variant::INT:
			p_worked.evaluator(left, right, &out.integer);
			r_result->l = out.integer;
			return true;
		default: {
			Variant *made = result_handle(p_result, p_worked);
			p_worked.evaluator(left, right, inside(*made, p_worked.result));
			r_result->p = made;
			return true;
		}
	}
}

bool GodotHost::run_builtin(const Worked &p_worked, const vm::Argument *p_operands, size_t p_count, const vm::Wanted &p_result, vm::Slot *r_result,
		std::string *r_error) {
	const size_t first = p_worked.is_static ? 0 : 1;
	Scalar scalars[MOST];
	Values values;
	GDExtensionConstTypePtr pointers[MOST];
	void *receiver = nullptr;
	if (!p_worked.is_static) {
		Variant *held = p_operands[0].kind == ir::Kind::Val ? static_cast<Variant *>(p_operands[0].slot.p) : nullptr;
		if (!held || held->get_type() != p_worked.receiver) {
			return false;
		}
		receiver = inside(*held, p_worked.receiver);
	}
	for (size_t i = first; i < p_count; i++) {
		const int type = p_worked.parameters[i - first];
		const vm::Argument &given = p_operands[i];
		if (type == ANY_VALUE) {
			Variant &value = values.next();
			value = to_variant(given.kind, given.slot);
			pointers[i - first] = &value;
		} else if ((type == Variant::STRING || type == Variant::STRING_NAME) && given.kind == ir::Kind::Str && given.slot.p) {
			Variant &value = values.next();
			argument(p_worked, i - first, given, &value);
			pointers[i - first] = inside(value, type);
		} else if (!(pointers[i - first] = bare(given, type, &scalars[i]))) {
			return false;
		}
	}
	if (p_worked.result == Variant::OBJECT) {
		// A value type's method that gives an object (Signal's get_object) writes the bare pointer. Written
		// inside a Variant it would be an object with no id, which reads as none: the Variant is made of
		// the object instead.
		GDExtensionObjectPtr object = nullptr;
		p_worked.builtin(receiver, pointers, &object, (int)(p_count - first));
		const Variant result = object ? Variant(internal::get_object_instance_binding(object)) : Variant();
		return take(p_result, result, p_worked.name, r_result, r_error);
	}
	if (r_result && into_handle(p_result, p_worked.result)) {
		Variant *made = result_handle(p_result, p_worked);
		p_worked.builtin(receiver, pointers, inside(*made, p_worked.result), (int)(p_count - first));
		r_result->p = made;
		return true;
	}
	Variant result = p_worked.result == ANY_VALUE ? Variant() : p_worked.zero;
	void *out = p_worked.result == NO_VALUE ? nullptr : p_worked.result == ANY_VALUE ? (void *)&result : inside(result, p_worked.result);
	p_worked.builtin(receiver, pointers, out, (int)(p_count - first));
	return take(p_result, result, p_worked.name, r_result, r_error);
}

bool GodotHost::run_bound(const Worked &p_worked, const vm::Argument *p_operands, size_t p_count, const vm::Wanted &p_result, vm::Slot *r_result,
		std::string *r_error) {
	GDExtensionObjectPtr object = nullptr;
	const size_t first = p_worked.is_static ? 0 : 1;
	if (!p_worked.is_static && !object_of(p_operands[0], &object)) {
		return false;
	}
	if (!p_worked.is_static && !object) {
		*r_error = FREED;
		return false;
	}
	if (p_worked.bare) {
		if (const std::optional<bool> done = run_bare(p_worked, object, p_operands + first, p_count - first, p_result, r_result)) {
			return *done;
		}
	}
	Values values;
	const Variant *pointers[MOST];
	size_t count = 0;
	if (p_worked.index >= 0) {
		Variant &index = values.next();
		index = p_worked.index;
		pointers[count++] = &index;
	}
	if (p_count - first + count > MOST) {
		return false;
	}
	for (size_t i = first; i < p_count; i++, count++) {
		Variant &value = values.next();
		argument(p_worked, count, p_operands[i], &value);
		pointers[count] = &value;
	}
	Variant result;
	GDExtensionCallError error;
	error.error = GDEXTENSION_CALL_OK;
	gdextension_interface::object_method_bind_call(p_worked.bound, object, reinterpret_cast<GDExtensionConstVariantPtr *>(pointers), (GDExtensionInt)count,
			result._native_ptr(), &error);
	if (error.error != GDEXTENSION_CALL_OK) {
		*r_error = describe(error, p_worked.name);
		return false;
	}
	return p_worked.what == What::Setter || take(p_result, result, p_worked.name, r_result, r_error);
}

// One of Godot's value types made from its arguments, by Godot's constructor for their types; into the
// handle the result's register holds, where it holds one.
bool GodotHost::run_construct(const Worked &p_worked, const vm::Argument *p_operands, size_t p_count, const vm::Wanted &p_result, vm::Slot *r_result,
		std::string *r_error) {
	if (!r_result) {
		return false;
	}
	Values values;
	const Variant *pointers[MOST];
	for (size_t i = 0; i < p_count; i++) {
		Variant &value = values.next();
		value = to_variant(p_operands[i].kind, p_operands[i].slot);
		pointers[i] = &value;
	}
	Variant made;
	GDExtensionCallError error;
	error.error = GDEXTENSION_CALL_OK;
	// `made` holds nothing yet (it is null), so making the value over it lets go of nothing.
	gdextension_interface::variant_construct((GDExtensionVariantType)p_worked.result, made._native_ptr(),
			reinterpret_cast<GDExtensionConstVariantPtr *>(pointers), (int32_t)p_count, &error);
	if (error.error != GDEXTENSION_CALL_OK) {
		*r_error = describe(error, p_worked.name);
		return false;
	}
	Variant *held = static_cast<Variant *>(p_result.reuse);
	if (held && held->get_type() == p_worked.result) {
		*held = made;
		r_result->p = held;
	} else {
		r_result->p = make_handle(made);
	}
	return true;
}

bool GodotHost::run_field(const Worked &p_worked, const vm::Argument *p_operands, const vm::Wanted &p_result, vm::Slot *r_result, std::string *r_error) {
	Variant *held = p_operands[0].kind == ir::Kind::Val ? static_cast<Variant *>(p_operands[0].slot.p) : nullptr;
	if (!held || held->get_type() != p_worked.receiver) {
		return false;
	}
	if (p_worked.what == What::FieldSet) {
		Scalar scalar;
		const void *value = bare(p_operands[1], p_worked.result, &scalar);
		if (value) {
			p_worked.setter(inside(*held, p_worked.receiver), value);
		}
		return value != nullptr;
	}
	if (r_result && p_result.kind == ir::Kind::F64 && p_worked.result == Variant::FLOAT) {
		p_worked.getter(inside(*held, p_worked.receiver), &r_result->d); // A part that is a Number: no Variant at all.
		return true;
	}
	if (r_result && into_handle(p_result, p_worked.result)) {
		Variant *made = result_handle(p_result, p_worked);
		p_worked.getter(inside(*held, p_worked.receiver), inside(*made, p_worked.result));
		r_result->p = made;
		return true;
	}
	Variant result = p_worked.zero;
	p_worked.getter(inside(*held, p_worked.receiver), inside(result, p_worked.result));
	return take(p_result, result, p_worked.name, r_result, r_error);
}

namespace {

using Worked = GodotHost::Worked;

// One of Godot's classes' methods by its hash, if this Godot has it: one that is missing (a class of the
// editor's in an exported game) is left to be reported by name where it is used, not at load.
GDExtensionMethodBindPtr method_bind(std::string_view p_class, std::string_view p_method, std::string_view p_hash) {
	if (p_hash == "-" || p_class.empty()) {
		return nullptr;
	}
	const StringName klass(String::utf8(p_class.data(), (int64_t)p_class.size()));
	const StringName method(String::utf8(p_method.data(), (int64_t)p_method.size()));
	ClassDBSingleton *classes = ClassDBSingleton::get_singleton();
	if (!classes->class_exists(klass) || !classes->class_has_method(klass, method)) {
		return nullptr;
	}
	return gdextension_interface::classdb_get_method_bind(klass._native_ptr(), method._native_ptr(), std::strtoll(std::string(p_hash).c_str(), nullptr, 10));
}

// An operator for the types its operands and its result are held as.
std::unique_ptr<Worked> operator_of(const ir::Function &p_function, const ir::Instruction &p_in, const std::vector<std::string_view> &p_words,
		std::string *r_key) {
	const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
	const ir::Kind out = p_function.registers[p_in.a];
	const std::optional<int> left = type_held(p_function, list[0]);
	const std::optional<int> right = list.size() == 2 ? type_held(p_function, list[1]) : std::optional<int>((int)Variant::NIL);
	const std::optional<int> result = out == ir::Kind::Bool ? std::optional<int>((int)Variant::BOOL) : type_held(p_function, p_in.a);
	if (list.size() > 2 || p_words.size() < 2 || !left || !right || !result || (out == ir::Kind::I32 || out == ir::Kind::U32)) {
		return nullptr;
	}
	// Godot checks a whole-number divisor for zero only where it can say so (its checked evaluate): dividing
	// by one, or taking what is left over, goes by name, so that a zero is the error it is.
	const int op = std::atoi(std::string(p_words[1]).c_str());
	const bool whole = *right == Variant::INT || *right == Variant::VECTOR2I || *right == Variant::VECTOR3I || *right == Variant::VECTOR4I;
	if (whole && (op == GDEXTENSION_VARIANT_OP_DIVIDE || op == GDEXTENSION_VARIANT_OP_MODULE)) {
		return nullptr;
	}
	auto worked = std::make_unique<Worked>();
	worked->what = Worked::What::Operator;
	worked->name = std::string(p_words[0]);
	worked->evaluator = gdextension_interface::variant_get_ptr_operator_evaluator((GDExtensionVariantOperator)op,
			(GDExtensionVariantType)*left, (GDExtensionVariantType)*right);
	worked->operands[0] = *left;
	worked->operands[1] = *right;
	worked->result = *result;
	worked->zero = zero_of(*result);
	*r_key += " " + std::to_string(*left) + " " + std::to_string(*right) + " " + std::to_string(*result);
	return worked->evaluator ? std::move(worked) : nullptr;
}

// A method: of one of Godot's value types (called on the bare value; `p_receiver` NO_VALUE for a static
// one), of one of its classes (by its method bind), or one of its global functions.
std::unique_ptr<Worked> method_of(const ir::EngineName &p_entry, const std::vector<std::string_view> &p_words, int p_receiver, bool p_static,
		size_t p_arguments) {
	if (p_words.size() < 5 || p_words[1] != "method" || p_words[2] == "-") {
		return nullptr;
	}
	auto worked = std::make_unique<Worked>();
	worked->name = std::string(p_words[0]);
	worked->parameters = types_named(p_words[4]);
	worked->names.resize(worked->parameters.size());
	worked->is_static = p_static;
	worked->result = type_named(p_words[3]);
	worked->zero = zero_of(worked->result);
	const std::optional<int> value_type_of_owner = value_type(p_entry.owner);
	const bool varargs = p_words.size() > 5;
	if (!value_type_of_owner) {
		worked->what = Worked::What::Bound;
		worked->bound = method_bind(p_entry.owner, p_words[0], p_words[2]);
		worked->bare = !varargs && (worked->result == NO_VALUE || held_bare(worked->result)) &&
				std::all_of(worked->parameters.begin(), worked->parameters.end(), held_bare);
		return worked->bound ? std::move(worked) : nullptr;
	}
	// Called on the bare value, a value type's method takes every argument, of its exact type.
	if (varargs || worked->parameters.size() != p_arguments || (!p_static && p_receiver != *value_type_of_owner)) {
		return nullptr;
	}
	worked->what = Worked::What::Builtin;
	worked->receiver = *value_type_of_owner;
	const StringName method(String::utf8(p_words[0].data(), (int64_t)p_words[0].size()));
	worked->builtin = gdextension_interface::variant_get_ptr_builtin_method((GDExtensionVariantType)*value_type_of_owner, method._native_ptr(),
			std::strtoll(std::string(p_words[2]).c_str(), nullptr, 10));
	return worked->builtin ? std::move(worked) : nullptr;
}

// A property of one of Godot's classes, by its accessor's method bind; or a part of one of its values.
std::unique_ptr<Worked> member_of(const ir::EngineName &p_entry, const std::vector<std::string_view> &p_words, bool p_set, ir::Kind p_receiver) {
	auto worked = std::make_unique<Worked>();
	worked->name = std::string(p_words[0]);
	if (p_receiver == ir::Kind::Ref && p_words.size() == 8 && p_words[1] == "property") {
		worked->what = p_set ? Worked::What::Setter : Worked::What::Getter;
		worked->bound = method_bind(p_entry.owner, p_words[p_set ? 5 : 3], p_words[p_set ? 6 : 4]);
		worked->index = p_words[7] == "-" ? -1 : std::atoll(std::string(p_words[7]).c_str());
		// What the accessor takes: the index first, if it has one, then a setter's value; a getter gives it.
		const int type = type_named(p_words[2]);
		if (worked->index >= 0) {
			worked->parameters.push_back(Variant::INT);
		}
		if (p_set) {
			worked->parameters.push_back(type);
		}
		worked->names.resize(worked->parameters.size());
		worked->result = p_set ? NO_VALUE : type;
		worked->zero = zero_of(worked->result);
		worked->bare = held_bare(type);
		return worked->bound ? std::move(worked) : nullptr;
	}
	const std::optional<int> owner = value_type(p_entry.owner);
	if (p_receiver != ir::Kind::Val || p_words.size() != 3 || p_words[1] != "field" || !owner) {
		return nullptr;
	}
	const StringName member(String::utf8(p_words[0].data(), (int64_t)p_words[0].size()));
	worked->what = p_set ? Worked::What::FieldSet : Worked::What::FieldGet;
	worked->receiver = *owner;
	worked->result = type_named(p_words[2]);
	worked->zero = zero_of(worked->result);
	worked->getter = gdextension_interface::variant_get_ptr_getter((GDExtensionVariantType)*owner, member._native_ptr());
	worked->setter = gdextension_interface::variant_get_ptr_setter((GDExtensionVariantType)*owner, member._native_ptr());
	return worked->result >= 0 && (p_set ? worked->setter != nullptr : worked->getter != nullptr) ? std::move(worked) : nullptr;
}

} // namespace

std::unique_ptr<GodotHost::Worked> GodotHost::work_out(const ir::Module &p_module, const ir::Function &p_function, const ir::Instruction &p_in,
		std::string *r_key) {
	// Only these are worked out; each names its entry in its second operand, but HostGet in its third.
	const ir::Op op = p_in.op;
	if (op != ir::Op::HostOperate && op != ir::Op::HostCall && op != ir::Op::HostCallStatic && op != ir::Op::HostGet && op != ir::Op::HostSet &&
			op != ir::Op::HostConstant && op != ir::Op::HostNew) {
		return nullptr;
	}
	const bool member = op == ir::Op::HostGet;
	const ir::EngineName &entry = p_module.engine_names[member ? p_in.c : p_in.b];
	const std::vector<std::string_view> words = godot_words(entry.value);
	*r_key = std::to_string((int)p_in.op) + " " + entry.key;
	switch (p_in.op) {
		case ir::Op::HostOperate:
			return operator_of(p_function, p_in, words, r_key);
		case ir::Op::HostCall: {
			const std::vector<uint32_t> &list = p_function.argument_lists[p_in.c];
			const std::optional<int> receiver = type_held(p_function, list[0]);
			*r_key += " " + std::to_string(receiver.value_or(NO_VALUE)) + " " + std::to_string(list.size());
			return method_of(entry, words, receiver.value_or(NO_VALUE), false, list.size() - 1);
		}
		case ir::Op::HostCallStatic: {
			const size_t count = p_function.argument_lists[p_in.c].size();
			*r_key += " " + std::to_string(count);
			if (entry.owner.empty()) {
				std::string error;
				auto worked = std::make_unique<Worked>();
				worked->what = Worked::What::Utility;
				worked->utility = utility(entry.value, &error);
				return worked->utility ? std::move(worked) : nullptr;
			}
			return method_of(entry, words, NO_VALUE, true, count);
		}
		case ir::Op::HostGet:
		case ir::Op::HostSet: {
			const ir::Kind receiver = p_function.registers[member ? p_in.b : p_in.a];
			*r_key += " " + std::to_string((int)receiver);
			return member_of(entry, words, !member, receiver);
		}
		case ir::Op::HostNew: {
			// One of Godot's value types (Vector2(x, y)); an object of one of its classes is made by name.
			const std::optional<int> type = value_type(entry.name);
			if (!type) {
				return nullptr;
			}
			auto worked = std::make_unique<Worked>();
			worked->what = Worked::What::Construct;
			worked->name = "new " + entry.name;
			worked->result = *type;
			worked->zero = zero_of(*type);
			return worked;
		}
		case ir::Op::HostConstant: {
			const std::optional<int> type = value_type(entry.owner);
			auto worked = std::make_unique<Worked>();
			worked->name = entry.name;
			if (type) {
				gdextension_interface::variant_get_constant_value((GDExtensionVariantType)*type, name(entry.name)._native_ptr(), worked->value._native_ptr());
			}
			// Godot answers a name it does not have with -1; none asked for here is a number.
			const Variant::Type got = worked->value.get_type();
			return got == Variant::NIL || got == Variant::INT ? nullptr : std::move(worked);
		}
		default:
			return nullptr;
	}
}

const GodotHost::Worked *GodotHost::keep(std::string p_key, std::unique_ptr<Worked> p_worked) {
	auto [at, added] = worked_.try_emplace(std::move(p_key), std::move(p_worked));
	return at->second.get();
}

const vm::Prepared *GodotHost::prepare(const ir::Module &p_module, const ir::Function &p_function, const ir::Instruction &p_in) {
	std::string key;
	std::unique_ptr<Worked> worked = work_out(p_module, p_function, p_in, &key);
	if (!worked) {
		return nullptr;
	}
	// One that takes a StringName keeps the name it made for its own call: `isActionPressed("left")` and
	// `isActionPressed("right")` each keep theirs, and neither is made again.
	if (std::find(worked->parameters.begin(), worked->parameters.end(), (int)Variant::STRING_NAME) != worked->parameters.end()) {
		key += " at " + std::to_string(reinterpret_cast<uintptr_t>(&p_in));
		worked->own_place = true;
	}
	worked->program = program_;
	// The same request elsewhere, or in a program loaded again, is worked out once.
	if (auto found = worked_.find(key); found != worked_.end()) {
		found->second->program = program_;
		return found->second.get();
	}
	return keep(std::move(key), std::move(worked));
}

void GodotHost::release_unused() {
	for (auto at = worked_.begin(); at != worked_.end();) {
		at = at->second->own_place && at->second->program != program_ ? worked_.erase(at) : std::next(at);
	}
}

std::pair<size_t, size_t> GodotHost::own_places() const {
	size_t kept = 0, used = 0;
	for (const auto &[key, worked] : worked_) {
		kept += worked->own_place ? 1 : 0;
		used += worked->own_place && worked->program == program_ ? 1 : 0;
	}
	return { kept, used };
}

bool GodotHost::run(const vm::Prepared &p_prepared, const vm::Argument *p_operands, size_t p_count, const vm::Wanted &p_result, vm::Slot *r_result,
		std::string *r_error) {
	const Worked &worked = static_cast<const Worked &>(p_prepared);
	if (p_count > MOST) {
		return false;
	}
	switch (worked.what) {
		case What::Operator:
			return run_operator(worked, p_operands, p_count, p_result, r_result);
		case What::Builtin:
			return run_builtin(worked, p_operands, p_count, p_result, r_result, r_error);
		case What::Bound:
		case What::Getter:
		case What::Setter:
			return run_bound(worked, p_operands, p_count, p_result, r_result, r_error);
		case What::FieldGet:
		case What::FieldSet:
			return run_field(worked, p_operands, p_result, r_result, r_error);
		case What::Constant:
			return take(p_result, worked.value, worked.name, r_result, r_error);
		case What::Utility:
			return call_utility(*worked.utility, p_operands, p_count, p_result, r_result, r_error);
		case What::Construct:
			return run_construct(worked, p_operands, p_count, p_result, r_result, r_error);
	}
	return false;
}

} // namespace kas3
