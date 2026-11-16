// A method of the library called by name on an untyped value (`u.push(1)`, `u.charAt(0)`), from its row in
// ir/library_methods.h (vm/library_calls.cpp). One helper class, a friend of the interpreter, so that the
// interpreter's own header does not grow. Nothing outside that file includes this.
//
// The arguments are converted as the row says into registers of their own, set past the running frame's,
// and the row's typed operation is run on them: the very code a typed call runs. Nothing here is on the
// path of typed code.
#pragma once

#include "interpreter_internal.h"
#include "library_methods.h"

#include <string>
#include <vector>

namespace kas3 {
namespace vm {

class LibraryCall {
	Interpreter &vm_;
	const ir::LibraryMethod &method_;
	const Slot holder_; // Held by the caller for as long as this is.
	const std::vector<Slot> &given_;
	const uint32_t out_; // The running frame's register for what the method gives (untyped), or NONE.

	const ir::Class *list_class() const; // The class of the Array or Vector it is called on; null for any other.
	bool counted() const; // Whether as many arguments were given as the method takes: AS3's ArgumentError #1063 if not.
	// One argument as the method takes it, with a reference of its own: false if it raised.
	bool converted(const ir::Taken *p_fixed, ir::Takes p_takes, size_t p_index, ir::Kind *r_kind, Slot *r_value);
	void typed();
	void each();
	void invoke();
	void give(ir::Kind p_kind, Slot p_owned);

public:
	LibraryCall(Interpreter &p_vm, const ir::LibraryMethod &p_method, Slot p_holder, const std::vector<Slot> &p_given, uint32_t p_out) :
			vm_(p_vm), method_(p_method), holder_(p_holder), given_(p_given), out_(p_out) {}

	// The method of that name that such a value has, or null.
	static const ir::LibraryMethod *find(const Interpreter &p_vm, Slot p_holder, std::string_view p_name);
	// Done, or raised: the interpreter's error says which.
	void run();
};

} // namespace vm
} // namespace kas3
