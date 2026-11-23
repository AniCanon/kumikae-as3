// What an untyped value does with Godot by name, found out as the program runs (vm/engine_named.cpp): a
// class of Godot's held as a class value made new. One helper class, a friend of the interpreter, so that
// the interpreter's own header does not grow. Nothing on the path of typed code is here.
#pragma once

#include "interpreter_internal.h"

#include <vector>

namespace kas3 {
namespace vm {

class EngineNamed {
	Interpreter &vm_;

public:
	explicit EngineNamed(Interpreter &p_vm) :
			vm_(p_vm) {}

	// `new c(a)` where c is one of Godot's classes (its engine name): what `new` of the class named asks of
	// the engine, a new object of the class or a new value of the value type, with the arguments as they
	// are held. The running frame's register p_out takes it, untyped.
	void construct(uint32_t p_name, const std::vector<Slot> &p_given, uint32_t p_out);
};

} // namespace vm
} // namespace kas3
