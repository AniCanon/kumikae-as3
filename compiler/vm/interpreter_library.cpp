// What the library's functions of plain values still ask of the interpreter itself (vm/library.h,
// Unmoved): each is the interpreter's own member, until its family is a function of plain values too.
#include "interpreter_internal.h"

namespace kas3 {
namespace vm {

using ir::Kind;

bool Interpreter::Asked::element_is(Kind p_kind, Slot p_element, Slot p_value) {
	return vm.element_is(p_kind, p_element, p_value);
}

bool Interpreter::Asked::object_of_engine(Slot p_value, uint32_t p_class, bool *r_taken, Slot *r_out) {
	Object *made = nullptr;
	if (vm.engine_array(p_value, p_class)) {
		made = vm.array_of_engine(any::pointer(p_value), p_class);
	} else if (vm.engine_dictionary(p_value, p_class)) {
		made = vm.table_of_engine(any::pointer(p_value), p_class);
	} else {
		return false;
	}
	*r_taken = true;
	r_out->p = made;
	return made != nullptr;
}

bool Interpreter::Asked::engine_is(Slot p_value, uint32_t p_class) {
	return vm.engine_array(p_value, p_class) || vm.engine_dictionary(p_value, p_class);
}

bool Interpreter::Asked::array_number_get(Vector *p_array, double p_number, Slot *r_out) {
	vm.array_number_get(p_array, p_number, *r_out);
	return !vm.failing_;
}

bool Interpreter::Asked::array_number_set(Vector *p_array, double p_number, Slot p_value) {
	vm.array_number_set(p_array, p_number, p_value);
	return !vm.failing_;
}

bool Interpreter::Asked::sort_field(Slot p_element, Str *p_name, Slot *r_value) {
	return vm.sort_field(p_element, p_name, r_value);
}

} // namespace vm
} // namespace kas3
