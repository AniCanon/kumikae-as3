// What the library's functions of plain values still ask of the interpreter itself (vm/library.h,
// Unmoved): each is the interpreter's own member, until its family is a function of plain values too.
#include "interpreter_internal.h"

namespace kas3 {
namespace vm {

using ir::Kind;

bool Interpreter::Asked::element_is(Kind p_kind, Slot p_element, Slot p_value) {
	return vm.element_is(p_kind, p_element, p_value);
}

Slot Interpreter::Asked::to_any(Kind p_kind, Slot p_value) {
	return vm.to_any(p_kind, p_value);
}

bool Interpreter::Asked::from_any(Slot p_value, Kind p_kind, uint32_t p_check, Slot *r_out) {
	return vm.from_any(p_value, p_kind, p_check, r_out);
}

bool Interpreter::Asked::coercion_failed(Slot p_value, Kind p_kind, uint32_t p_check) {
	return vm.coercion_failed(p_value, p_kind, p_check);
}

bool Interpreter::Asked::array_number_get(Vector *p_array, double p_number, Slot *r_out) {
	vm.array_number_get(p_array, p_number, *r_out);
	return !vm.failing_;
}

bool Interpreter::Asked::array_number_set(Vector *p_array, double p_number, Slot p_value) {
	vm.array_number_set(p_array, p_number, p_value);
	return !vm.failing_;
}

bool Interpreter::Asked::vector_text(Vector *p_vector, std::string_view p_separator, std::string *r_text, bool p_locale) {
	return vm.vector_text(p_vector, p_separator, r_text, p_locale);
}

bool Interpreter::Asked::sort_field(Slot p_element, Str *p_name, Slot *r_value) {
	return vm.sort_field(p_element, p_name, r_value);
}

} // namespace vm
} // namespace kas3
