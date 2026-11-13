// What the interpreter needs from where it runs. Inside Godot the engine supplies it; `kas3c run` and the
// tests use the stand-in in standalone_host.cpp. Nothing else in vm/ may name the engine.
#pragma once

#include "ir.h"
#include "runtime.h"

#include <string>
#include <string_view>

namespace kas3 {
namespace vm {

// Everything here is defined in this header, so that a host may be built with or without run-time type
// information whatever the interpreter was built with.
struct Host {
	virtual ~Host() {}
	// One traced line, without its line break.
	virtual void print(std::string_view p_text) = 0;
	// An error nothing caught: its text and stack trace, several lines.
	virtual void error(std::string_view p_text) = 0;

	// ---- the engine ----
	//
	// An engine value (a Vector2, a node) is a handle only the host understands. The interpreter copies
	// and drops handles through the host and never looks inside one. A null handle is null.
	// Names are Godot's own. On failure these return false (or null) and say why in r_error; the
	// interpreter turns that into an AS3 error. Without an engine, every one of them fails.

	// A value going to the engine: a number, a Boolean, a string (a Str *, or null), or a handle.
	struct Argument {
		ir::Kind kind;
		Slot slot;
	};
	// A new value of one of Godot's value types. The handle is the caller's to drop.
	virtual void *construct(std::string_view p_type, const Argument *p_arguments, size_t p_count, std::string *r_error) {
		(void)p_type, (void)p_arguments, (void)p_count;
		*r_error = "there is no engine here";
		return nullptr;
	}
	// Calls a method. The result arrives as p_result kind: a string as a new Str, an engine value as a
	// handle, both the caller's. r_result is null when the result is not wanted.
	// What the interpreter wants back: the kind, and for an engine value Godot's name for its type
	// ("Vector2", "Node"; empty when it is not known). The host refuses what does not fit.
	struct Wanted {
		ir::Kind kind = ir::Kind::Void;
		std::string_view type;
	};
	virtual bool call(void *p_receiver, std::string_view p_method, const Argument *p_arguments, size_t p_count,
			const Wanted &p_result, Slot *r_result, std::string *r_error) {
		(void)p_receiver, (void)p_method, (void)p_arguments, (void)p_count, (void)p_result, (void)r_result;
		*r_error = "there is no engine here";
		return false;
	}
	virtual bool get(void *p_receiver, std::string_view p_property, const Wanted &p_result, Slot *r_result, std::string *r_error) {
		(void)p_receiver, (void)p_property, (void)p_result, (void)r_result;
		*r_error = "there is no engine here";
		return false;
	}
	// An operator as AS3 writes it ("+", "==", "<"), on one operand or two, at least one of them an engine
	// value. A null handle is null.
	virtual bool operate(std::string_view p_operator, const Argument *p_arguments, size_t p_count, const Wanted &p_result,
			Slot *r_result, std::string *r_error) {
		(void)p_operator, (void)p_arguments, (void)p_count, (void)p_result, (void)r_result;
		*r_error = "there is no engine here";
		return false;
	}
	virtual bool set(void *p_receiver, std::string_view p_property, const Argument &p_value, std::string *r_error) {
		(void)p_receiver, (void)p_property, (void)p_value;
		*r_error = "there is no engine here";
		return false;
	}
	// Another handle for the same value (for a value type, a copy of it), and giving one up.
	virtual void *copy(void *p_value) { return p_value; }
	virtual void drop(void *p_value) { (void)p_value; }
};

// Writes traces to standard output and errors to standard error.
struct StandaloneHost final : Host {
	void print(std::string_view p_text) override;
	void error(std::string_view p_text) override;
};

} // namespace vm
} // namespace kas3
