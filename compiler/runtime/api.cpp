// The table a program library is handed (api.h), and the functions in it that are the interpreter's own
// values and numbers under the runtime's names.
#include "native_host.h"

#include "numeric.h"

namespace kas3 {
namespace rt {

void free_string(Context *, Str *p_string) {
	vm::free_string(p_string);
}

Str *join_text(Context *, const Str *p_first, const Str *p_second) {
	return vm::join_strings(p_first ? p_first->view() : std::string_view("null"), p_second ? p_second->view() : std::string_view("null"));
}

int compare_text(Context *, const Str *p_a, const Str *p_b) {
	return vm::compare_utf16(p_a->view(), p_b->view());
}

int32_t text_length(Context *, const Str *p_text) {
	return p_text ? p_text->length() : 0;
}

double text_to_number(Context *, const Str *p_text) {
	return p_text ? vm::string_to_number(p_text->view()) : 0.0;
}

Slot any_integer(Context *, int32_t p_value) {
	return vm::any::integer(p_value);
}

Slot any_number(Context *, double p_value) {
	return vm::any::number(p_value);
}

Slot any_boolean(Context *, bool p_value) {
	return vm::any::boolean(p_value);
}

Slot any_string(Context *, Str *p_string) {
	return vm::any::string(p_string);
}

Slot any_object(Context *, Object *p_object) {
	return vm::any::object(p_object);
}

Slot any_null(Context *) {
	return vm::any::null();
}

Slot own_engine_any(Context *, void *p_handle, bool p_ref) {
	return p_ref ? vm::any::godot_object(p_handle) : vm::any::godot_value(p_handle);
}

void *any_pointer(Context *, Slot p_any) {
	return vm::any::pointer(p_any);
}

int32_t to_int32(Context *, double p_value) {
	return vm::to_int32(p_value);
}

uint32_t to_uint32(Context *, double p_value) {
	return vm::to_uint32(p_value);
}

int64_t clamp_wide(Context *, double p_value) {
	return vm::clamp_wide(p_value);
}

double compare_wide(Context *, int64_t p_wide, double p_number) {
	return vm::compare_wide(p_wide, p_number);
}

double math_unary(Context *, uint32_t p_function, double p_value) {
	return vm::math_unary(static_cast<ir::MathFunction>(p_function), p_value);
}

double math_pow(Context *, double p_base, double p_power) {
	return vm::math_pow(p_base, p_power);
}

double math_min(Context *, double p_a, double p_b) {
	return vm::math_min(p_a, p_b);
}

double math_max(Context *, double p_a, double p_b) {
	return vm::math_max(p_a, p_b);
}

double math_random(Context *) {
	return vm::math_random();
}

const Api *runtime_api() {
	static const Api table = {
#define X(name, result, parameters, arguments) &name,
		KAS3_RUNTIME_API(X)
#undef X
	};
	return &table;
}

} // namespace rt
} // namespace kas3
