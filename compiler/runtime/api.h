// Every function of the runtime that generated code calls out of line, said once. A program library has
// no symbol of the runtime's to find when it is loaded: the host hands it this table through the context
// (`c->api`), and in a program library each function below is an inline call through it. In the host
// itself (and nowhere else) they are the functions the table points at. Included by values.h alone.
//
// X(name, result, (parameters), (arguments)): the context is always first.
#pragma once

#define KAS3_RUNTIME_API(X) \
	/* The library's functions of plain values (direct.h). */ \
	X(text_call, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(text_of_value, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(text_joined, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(number_text, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(value_to_any, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(any_to_value, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(any_is_type, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(object_new, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(table_new, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(vector_call, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(vector_get, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(vector_set, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(vector_length, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(vector_set_length, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(vector_fixed, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(vector_set_fixed, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(vector_new, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(array_new, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(vector_from, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(text_method, bool, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(text_of_any, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(text_of_object, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(function_call, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(any_operate, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(function_make, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(method_make, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	X(function_length, void, (Context *c, uint32_t p_which, const Slot *p_values, uint32_t p_count, Slot *r_out), (c, p_which, p_values, p_count, r_out)) \
	/* Errors (operations.h says what each is for). */ \
	X(stack_overflow, bool, (Context *c), (c)) \
	X(overdue, bool, (Context *c), (c)) \
	X(raise_null, void, (Context *c), (c)) \
	X(call_failed, bool, (Context *c), (c)) \
	X(out_of_memory, void, (Context *c), (c)) \
	X(throw_value, void, (Context *c, Slot p_any), (c, p_any)) \
	X(rethrow, void, (Context *c, Slot p_any, uint32_t p_register, bool *r_traced), (c, p_any, p_register, r_traced)) \
	X(catches, bool, (Context *c, uint32_t p_type), (c, p_type)) \
	X(caught, Slot, (Context *c, uint32_t p_function, uint32_t p_line, uint32_t p_register, bool p_rethrows, bool *r_traced), \
			(c, p_function, p_line, p_register, p_rethrows, r_traced)) \
	X(leave_failed, void, (Context *c, uint32_t p_function, uint32_t p_line, bool p_traced), (c, p_function, p_line, p_traced)) \
	X(error_stack, Str *, (Context *c, Object *p_error), (c, p_error)) \
	X(forget_kept, void, (Context *c), (c)) \
	X(use_class, void, (Context *c, uint32_t p_class), (c, p_class)) \
	X(is_class, bool, (const Context *c, const Object *p_object, uint32_t p_class), (c, p_object, p_class)) \
	X(thunk, Slot, (Context *c, uint32_t p_thunk, const Slot *p_given, Slot *r_updated), (c, p_thunk, p_given, r_updated)) \
	X(trace, void, (Context *c, Str *const *p_texts, uint32_t p_count), (c, p_texts, p_count)) \
	/* Lifetime. */ \
	X(free_object, void, (Context *c, Object *p_object), (c, p_object)) \
	X(free_string, void, (Context *c, Str *p_string), (c, p_string)) \
	X(release_any_slow, void, (Context *c, Slot p_value), (c, p_value)) \
	X(shared_any, Slot, (Context *c, Slot p_any), (c, p_any)) \
	X(copy_handle, void *, (Context *c, void *p_handle), (c, p_handle)) \
	X(drop_handle, void, (Context *c, void *p_handle), (c, p_handle)) \
	X(new_object, Object *, (Context *c, uint32_t p_class), (c, p_class)) \
	X(new_array, Object *, (Context *c, const Slot *p_values, uint32_t p_count), (c, p_values, p_count)) \
	X(arguments_of, Object *, (Context *c, uint32_t p_function, const Slot *p_parameters, uint32_t p_given, Object *p_callee), \
			(c, p_function, p_parameters, p_given, p_callee)) \
	/* The fast paths that are not inline (fast.h says what each takes and leaves). */ \
	X(element_added, void *, (Context *c, Object *p_vector, uint32_t p_index), (c, p_vector, p_index)) \
	X(holder_of_any, Object *, (Context *c, Slot p_any), (c, p_any)) \
	X(key_of_any, Str *, (Context *c, Slot p_any), (c, p_any)) \
	X(property, Slot *, (const Context *c, Object *p_holder, const Str *p_key), (c, p_holder, p_key)) \
	X(set_property, bool, (Context *c, Object *p_holder, Str *p_key, Slot p_value), (c, p_holder, p_key, p_value)) \
	X(text_of_int, Str *, (Context *c, int32_t p_value), (c, p_value)) \
	X(text_of_uint, Str *, (Context *c, uint32_t p_value), (c, p_value)) \
	X(text_of_wide, Str *, (Context *c, int64_t p_value), (c, p_value)) \
	X(text_of_number, Str *, (Context *c, double p_value), (c, p_value)) \
	X(text_of_flag, Str *, (Context *c, bool p_value), (c, p_value)) \
	X(number_to_any, bool, (Context *c, double p_value, Slot *r_any), (c, p_value, r_any)) \
	X(from_any, bool, (const Context *c, uint8_t p_kind, uint32_t p_class, Slot p_any, Slot *r_value), (c, p_kind, p_class, p_any, r_value)) \
	X(has_members, bool, (Context *c, Slot p_any), (c, p_any)) \
	X(function_of_any, Object *, (Context *c, Slot p_any), (c, p_any)) \
	X(call_value, bool, (Context *c, Object *p_function, const Slot *p_arguments, uint32_t p_count, Slot *r_result), \
			(c, p_function, p_arguments, p_count, r_result)) \
	X(object_text, bool, (Context *c, Object *p_object, Str **r_text), (c, p_object, r_text)) \
	X(any_text, bool, (Context *c, Slot p_any, Str **r_text), (c, p_any, r_text)) \
	/* The interpreter's own values and numbers (vm/any.h, vm/runtime.h, vm/numeric.h), which a program \
	   library reaches here too. join_text reads a missing String as "null"; compare_text orders two \
	   Strings by their UTF-16 units; text_length counts them (0 for none); text_to_number is 0 for none. */ \
	X(join_text, Str *, (Context *c, const Str *p_first, const Str *p_second), (c, p_first, p_second)) \
	X(compare_text, int, (Context *c, const Str *p_a, const Str *p_b), (c, p_a, p_b)) \
	X(text_length, int32_t, (Context *c, const Str *p_text), (c, p_text)) \
	X(text_to_number, double, (Context *c, const Str *p_text), (c, p_text)) \
	X(any_integer, Slot, (Context *c, int32_t p_value), (c, p_value)) \
	X(any_number, Slot, (Context *c, double p_value), (c, p_value)) \
	X(any_boolean, Slot, (Context *c, bool p_value), (c, p_value)) \
	X(any_string, Slot, (Context *c, Str *p_string), (c, p_string)) \
	X(any_object, Slot, (Context *c, Object *p_object), (c, p_object)) \
	X(any_null, Slot, (Context *c), (c)) \
	X(any_pointer, void *, (Context *c, Slot p_any), (c, p_any)) \
	X(to_int32, int32_t, (Context *c, double p_value), (c, p_value)) \
	X(to_uint32, uint32_t, (Context *c, double p_value), (c, p_value)) \
	X(clamp_wide, int64_t, (Context *c, double p_value), (c, p_value)) \
	X(compare_wide, double, (Context *c, int64_t p_wide, double p_number), (c, p_wide, p_number)) \
	X(math_unary, double, (Context *c, uint32_t p_function, double p_value), (c, p_function, p_value)) \
	X(math_pow, double, (Context *c, double p_base, double p_power), (c, p_base, p_power)) \
	X(math_min, double, (Context *c, double p_a, double p_b), (c, p_a, p_b)) \
	X(math_max, double, (Context *c, double p_a, double p_b), (c, p_a, p_b)) \
	X(math_random, double, (Context *c), (c))

namespace kas3 {
namespace rt {

struct Api {
#define X(name, result, parameters, arguments) result (*name) parameters;
	KAS3_RUNTIME_API(X)
#undef X
};

#ifdef KAS3_PROGRAM_LIBRARY
#define X(name, result, parameters, arguments) \
	inline result name parameters { \
		return c->api->name arguments; \
	}
KAS3_RUNTIME_API(X)
#undef X
#else
#define X(name, result, parameters, arguments) result name parameters;
KAS3_RUNTIME_API(X)
#undef X
// The table of them, for a host to put in its context.
const Api *runtime_api();
#endif

} // namespace rt
} // namespace kas3
