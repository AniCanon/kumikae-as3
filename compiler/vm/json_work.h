// JSON.parse and JSON.stringify on the interpreter's values (vm/json_methods.cpp, vm/json_stringify.cpp),
// by the steps of ECMA-262 5th edition (15.12.2, 15.12.3). One helper class, a friend of the interpreter,
// so that the interpreter's own header does not grow. Nothing outside those two files includes this.
//
// A reviver, a replacer, a toJSON() and a getter run the program: anything may change meanwhile. Every
// value worked on is held with a reference of its own, keys are copied before a container is gone
// through, and no position in a table is kept across a call.
#pragma once

#include "interpreter_internal.h"
#include "json_text.h"

#include <optional>
#include <string>
#include <vector>

namespace kas3 {
namespace vm {

class Json final : public json::Builder {
	Interpreter &vm_;

	// Reading: the containers still open, innermost last, each held, with the name its next value takes.
	struct Open {
		Object *container;
		std::string key;
	};
	std::vector<Open> building_;
	Slot root_ = any::undefined();
	Object *reviver_ = nullptr;
	bool add(Slot p_owned);
	void abandon();
	std::optional<Slot> walk(Slot p_owned, std::string_view p_name, size_t p_depth);

	// Writing.
	enum class Wrote { Value, Nothing, Failed };
	Object *replacer_ = nullptr; // A function; or the keys of an Array, if listed_.
	bool listed_ = false;
	std::vector<std::string> keys_;
	std::string gap_;
	std::string indent_;
	std::string out_;
	std::vector<const Object *> open_; // The objects being written, for a cycle.
	bool options(Slot p_replacer, Slot p_space);
	Wrote write(Slot p_owned, std::string_view p_key);
	Wrote value(Slot p_value);
	Wrote member(bool p_first, std::string_view p_key, bool p_named, Slot p_owned);
	Wrote container(Object *p_object);
	Wrote array(Vector *p_vector);
	Wrote object(Object *p_object);
	std::vector<std::string> names_of(Object *p_object) const;
	bool member_value(Object *p_object, const std::string &p_name, Slot *r_value);
	void close(bool p_any, char p_bracket);
	bool call(Object *p_function, std::string_view p_key, Slot p_value, bool p_two, Slot *r_value);

public:
	explicit Json(Interpreter &p_interpreter) :
			vm_(p_interpreter) {}
	~Json() override { abandon(); }

	// The value the text holds, revived, with a reference of its own; nothing if it raised. The text is an
	// untyped value: anything but a String is AS3's SyntaxError.
	std::optional<Slot> parse(Slot p_text, Object *p_reviver);
	// The text, a String with one reference, or null for nothing written; nothing if it raised. The
	// arguments are the caller's, held while this runs.
	std::optional<Str *> stringify(Slot p_value, Slot p_replacer, Slot p_space);
	// The value made what its toJSON(key) gives, if it has one: a Date its text, a Dictionary the text
	// "Dictionary" unless it holds a function of that name itself. False if it raised.
	bool to_json(Slot *r_owned, std::string_view p_key);

	bool null() override;
	bool boolean(bool p_value) override;
	bool number(double p_value) override;
	bool string(std::string_view p_text) override;
	bool begin_object() override;
	bool key(std::string_view p_text) override;
	bool end_object() override;
	bool begin_array() override;
	bool end_array() override;
};

} // namespace vm
} // namespace kas3
