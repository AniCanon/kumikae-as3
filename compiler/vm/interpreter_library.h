// The standard library's callbacks into the running interpreter.
#pragma once

#include "library.h"

namespace kas3::vm {

class Interpreter;

class InterpreterLibrary final : public Runner, public Unmoved {
	Interpreter &vm;

public:
	explicit InterpreterLibrary(Interpreter &p_vm) : vm(p_vm) {}
	void raise(uint32_t p_class, const std::string &p_message) override;
	void out_of_memory() override;
	bool failing() const override;
	bool run(uint32_t p_function, const Slot *p_arguments, size_t p_count, Slot *r_result) override;
	uint32_t carries() const override;
	bool object_of_engine(Slot p_value, uint32_t p_class, bool *r_taken, Slot *r_out) override;
	bool engine_is(Slot p_value, uint32_t p_class) override;
	bool array_number_get(Vector *p_array, double p_number, Slot *r_out) override;
	bool array_number_set(Vector *p_array, double p_number, Slot p_value) override;
	bool sort_field(Slot p_element, Str *p_name, Slot *r_value) override;
};

} // namespace kas3::vm
