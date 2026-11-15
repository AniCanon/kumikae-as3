// JSON text on its own, as ECMA-262 5th edition's JSON object reads and writes it (15.12): the grammar
// read into a builder's calls, and a string written as Quote() writes it. Nothing here knows the
// interpreter; vm/json_methods.cpp makes AS3's values of what is read.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace kas3 {
namespace vm {
namespace json {

// Objects and Arrays nest at most this deep, read or written: one more is AS3's Error #1023. The walk
// for a reviver and the writing go down a level at a time, so this keeps them off the end of the stack.
constexpr size_t MAX_DEPTH = 1000;

// What the text holds, in order. A String is given as AS3 holds one (vm/text_units.h), a number as its
// value. A call that gives false stops the reading (memory ran out, say).
class Builder {
public:
	virtual ~Builder() = default;
	virtual bool null() = 0;
	virtual bool boolean(bool p_value) = 0;
	virtual bool number(double p_value) = 0;
	virtual bool string(std::string_view p_text) = 0;
	virtual bool begin_object() = 0;
	virtual bool key(std::string_view p_text) = 0; // Before each value of an object.
	virtual bool end_object() = 0;
	virtual bool begin_array() = 0;
	virtual bool end_array() = 0;
};

enum class Fault {
	None,
	Syntax, // Not JSON text: AS3's SyntaxError #1132.
	TooDeep, // Nested deeper than MAX_DEPTH.
	Stopped, // The builder said to stop.
};

// Reads the whole text, which is AS3's (UTF-8 as vm/text_units.h holds it): white space, one value,
// white space. Every value the builder was given before a fault stays the builder's to let go of.
Fault parse(std::string_view p_text, Builder &p_builder);

// Adds the text between double quotes, as Quote() writes it: `"` and `\` escaped, the control characters
// as \b \f \n \r \t or \u00XX, everything else as it is.
void quote(std::string_view p_text, std::string *r_out);

} // namespace json
} // namespace vm
} // namespace kas3
