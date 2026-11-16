// The methods of a String, an Array, a Vector and a Function, by name: what each takes, what it gives, and
// the typed operation that does it. The lowerer reads the vectors' rows for a call on a typed vector; the
// interpreter reads every row for a call by name on an untyped value (`u.push(1)`), where it converts the
// arguments as the row says and runs that same operation. A back end that reads the IR does the same.
// (A String's rows say again what builtins/toplevel.as declares, which is what a typed call is lowered
// from.) Nothing here allocates.
#pragma once

#include "ir.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace kas3 {
namespace ir {

// What a method is called on, as bits: a row may serve several.
constexpr uint8_t ON_TEXT = 1, ON_ARRAY = 2, ON_VECTOR = 4, ON_FUNCTION = 8;

// How a method is done. The first three are the operation of that name, with `which` its second operand;
// its arguments are what it is called on, then the fixed ones, then the rest.
//   Each    forEach, map, filter, some, every: a loop that calls the function given (`which` an EachMethod)
//   Invoke  a Function's call and apply (`which` an InvokeMethod)
enum class Does : uint8_t { VecCall, StrCall, RegExpCall, Each, Invoke };
enum class EachMethod : uint8_t { ForEach, Map, Filter, Some, Every };
enum class InvokeMethod : uint8_t { Call, Apply };

// What an argument is converted to, or what the method gives.
//   Words     a String as String(x) gives it: "null" and "undefined" are words
//   Text      a String as a store into one gives it: null and undefined are null
//   Element   what the vector holds, checked as a store into it is
//   Own       a vector of the very class it is called on
//   Object    any object, or null
enum class Takes : uint8_t { Nothing, Int, Uint, Number, Flag, Text, Words, Any, Element, Own, Object };

struct Taken {
	Takes kind = Takes::Nothing;
	bool required = false;
	// When left out: the number, for a number; for an untyped one, undefined (0) or null (1); for a String,
	// the row's `text_left_out`.
	double left_out = 0;
};

constexpr Taken need(Takes p_kind) {
	return { p_kind, true, 0 };
}

constexpr Taken or_else(Takes p_kind, double p_left_out = 0) {
	return { p_kind, false, p_left_out };
}

struct LibraryMethod {
	std::string_view name;
	uint8_t on;
	Does does;
	uint8_t which;
	std::array<Taken, 2> fixed;
	Takes rest;
	Takes result;
	std::string_view text_left_out = std::string_view();
};

constexpr uint8_t ON_LIST = ON_ARRAY | ON_VECTOR;
constexpr double END = 2147483647.0; // "To the end", where a method takes a place in the text or the list.

constexpr uint8_t text_code(StringFunction p_function) {
	return static_cast<uint8_t>(p_function);
}
constexpr uint8_t pattern_code(RegExpFunction p_function) {
	return static_cast<uint8_t>(p_function);
}
constexpr uint8_t list_code(VectorMethod p_method) {
	return static_cast<uint8_t>(p_method);
}
constexpr uint8_t each_code(EachMethod p_method) {
	return static_cast<uint8_t>(p_method);
}

inline constexpr std::array<LibraryMethod, 43> LIBRARY_METHODS = { {
		{ "charAt", ON_TEXT, Does::StrCall, text_code(StringFunction::CharAt), { or_else(Takes::Number) }, Takes::Nothing, Takes::Text },
		{ "charCodeAt", ON_TEXT, Does::StrCall, text_code(StringFunction::CharCodeAt), { or_else(Takes::Number) }, Takes::Nothing, Takes::Number },
		{ "indexOf", ON_TEXT, Does::StrCall, text_code(StringFunction::IndexOf), { or_else(Takes::Text), or_else(Takes::Number) }, Takes::Nothing,
				Takes::Int, "undefined" },
		{ "lastIndexOf", ON_TEXT, Does::StrCall, text_code(StringFunction::LastIndexOf), { or_else(Takes::Text), or_else(Takes::Number, END) },
				Takes::Nothing, Takes::Int, "undefined" },
		{ "substr", ON_TEXT, Does::StrCall, text_code(StringFunction::Substr), { or_else(Takes::Number), or_else(Takes::Number, END) }, Takes::Nothing,
				Takes::Text },
		{ "substring", ON_TEXT, Does::StrCall, text_code(StringFunction::Substring), { or_else(Takes::Number), or_else(Takes::Number, END) },
				Takes::Nothing, Takes::Text },
		{ "slice", ON_TEXT, Does::StrCall, text_code(StringFunction::Slice), { or_else(Takes::Number), or_else(Takes::Number, END) }, Takes::Nothing,
				Takes::Text },
		{ "concat", ON_TEXT, Does::StrCall, text_code(StringFunction::Concat), {}, Takes::Words, Takes::Text },
		{ "toUpperCase", ON_TEXT, Does::StrCall, text_code(StringFunction::Upper), {}, Takes::Nothing, Takes::Text },
		{ "toLocaleUpperCase", ON_TEXT, Does::StrCall, text_code(StringFunction::Upper), {}, Takes::Nothing, Takes::Text },
		{ "toLowerCase", ON_TEXT, Does::StrCall, text_code(StringFunction::Lower), {}, Takes::Nothing, Takes::Text },
		{ "toLocaleLowerCase", ON_TEXT, Does::StrCall, text_code(StringFunction::Lower), {}, Takes::Nothing, Takes::Text },
		{ "localeCompare", ON_TEXT, Does::StrCall, text_code(StringFunction::LocaleCompare), { need(Takes::Text) }, Takes::Any, Takes::Int },
		{ "split", ON_TEXT, Does::StrCall, text_code(StringFunction::Split), { or_else(Takes::Any, 1), or_else(Takes::Number, END) }, Takes::Nothing,
				Takes::Object },
		{ "replace", ON_TEXT, Does::RegExpCall, pattern_code(RegExpFunction::Replace), { or_else(Takes::Any), or_else(Takes::Any) }, Takes::Nothing,
				Takes::Text },
		{ "match", ON_TEXT, Does::RegExpCall, pattern_code(RegExpFunction::Match), { or_else(Takes::Any) }, Takes::Nothing, Takes::Object },
		{ "search", ON_TEXT, Does::RegExpCall, pattern_code(RegExpFunction::Search), { or_else(Takes::Any) }, Takes::Nothing, Takes::Int },

		{ "push", ON_LIST, Does::VecCall, list_code(VectorMethod::Push), {}, Takes::Element, Takes::Uint },
		{ "unshift", ON_LIST, Does::VecCall, list_code(VectorMethod::Unshift), {}, Takes::Element, Takes::Uint },
		{ "pop", ON_LIST, Does::VecCall, list_code(VectorMethod::Pop), {}, Takes::Nothing, Takes::Element },
		{ "shift", ON_LIST, Does::VecCall, list_code(VectorMethod::Shift), {}, Takes::Nothing, Takes::Element },
		{ "indexOf", ON_LIST, Does::VecCall, list_code(VectorMethod::IndexOf), { need(Takes::Element), or_else(Takes::Int) }, Takes::Nothing,
				Takes::Int },
		{ "lastIndexOf", ON_LIST, Does::VecCall, list_code(VectorMethod::LastIndexOf), { need(Takes::Element), or_else(Takes::Int, END) },
				Takes::Nothing, Takes::Int },
		{ "reverse", ON_LIST, Does::VecCall, list_code(VectorMethod::Reverse), {}, Takes::Nothing, Takes::Own },
		{ "slice", ON_LIST, Does::VecCall, list_code(VectorMethod::Slice), { or_else(Takes::Int), or_else(Takes::Int, 16777215) }, Takes::Nothing,
				Takes::Own },
		// An Array's concat takes anything: an Array is spread, the rest added. A Vector's, vectors of its class.
		{ "concat", ON_ARRAY, Does::VecCall, list_code(VectorMethod::Concat), {}, Takes::Any, Takes::Own },
		{ "concat", ON_VECTOR, Does::VecCall, list_code(VectorMethod::Concat), {}, Takes::Own, Takes::Own },
		{ "join", ON_LIST, Does::VecCall, list_code(VectorMethod::Join), { or_else(Takes::Words) }, Takes::Nothing, Takes::Text, "," },
		{ "toString", ON_LIST, Does::VecCall, list_code(VectorMethod::Join), { or_else(Takes::Words) }, Takes::Nothing, Takes::Text, "," },
		{ "toLocaleString", ON_LIST, Does::VecCall, list_code(VectorMethod::LocaleText), {}, Takes::Nothing, Takes::Text },
		{ "insertAt", ON_LIST, Does::VecCall, list_code(VectorMethod::InsertAt), { need(Takes::Int), need(Takes::Element) }, Takes::Nothing,
				Takes::Nothing },
		{ "removeAt", ON_LIST, Does::VecCall, list_code(VectorMethod::RemoveAt), { need(Takes::Int) }, Takes::Nothing, Takes::Element },
		{ "splice", ON_LIST, Does::VecCall, list_code(VectorMethod::Splice), { need(Takes::Int), or_else(Takes::Uint, 4294967295.0) }, Takes::Element,
				Takes::Own },
		// A Vector's sort takes one thing; an Array's none to two, and gives the array, 0 or an Array of indices.
		{ "sort", ON_ARRAY, Does::VecCall, list_code(VectorMethod::Sort), {}, Takes::Any, Takes::Any },
		{ "sort", ON_VECTOR, Does::VecCall, list_code(VectorMethod::Sort), { need(Takes::Any) }, Takes::Nothing, Takes::Own },
		{ "sortOn", ON_ARRAY, Does::VecCall, list_code(VectorMethod::SortOn), {}, Takes::Any, Takes::Any },
		{ "forEach", ON_LIST, Does::Each, each_code(EachMethod::ForEach), { need(Takes::Any), or_else(Takes::Any, 1) }, Takes::Nothing, Takes::Nothing },
		{ "map", ON_LIST, Does::Each, each_code(EachMethod::Map), { need(Takes::Any), or_else(Takes::Any, 1) }, Takes::Nothing, Takes::Own },
		{ "filter", ON_LIST, Does::Each, each_code(EachMethod::Filter), { need(Takes::Any), or_else(Takes::Any, 1) }, Takes::Nothing, Takes::Own },
		{ "some", ON_LIST, Does::Each, each_code(EachMethod::Some), { need(Takes::Any), or_else(Takes::Any, 1) }, Takes::Nothing, Takes::Flag },
		{ "every", ON_LIST, Does::Each, each_code(EachMethod::Every), { need(Takes::Any), or_else(Takes::Any, 1) }, Takes::Nothing, Takes::Flag },

		{ "call", ON_FUNCTION, Does::Invoke, static_cast<uint8_t>(InvokeMethod::Call), { or_else(Takes::Any) }, Takes::Any, Takes::Any },
		{ "apply", ON_FUNCTION, Does::Invoke, static_cast<uint8_t>(InvokeMethod::Apply), { or_else(Takes::Any), or_else(Takes::Any) }, Takes::Nothing,
				Takes::Any },
} };

// The method of that name on such a thing (one of the ON_ bits), or null.
constexpr const LibraryMethod *library_method(std::string_view p_name, uint8_t p_on) {
	for (const LibraryMethod &method : LIBRARY_METHODS) {
		if ((method.on & p_on) != 0 && method.name == p_name) {
			return &method;
		}
	}
	return nullptr;
}

// The kind of register a taken or given value is held in. `Element` is the vector's own element kind.
constexpr Kind kind_taken(Takes p_takes, Kind p_element) {
	switch (p_takes) {
		case Takes::Int:
			return Kind::I32;
		case Takes::Uint:
			return Kind::U32;
		case Takes::Number:
			return Kind::F64;
		case Takes::Flag:
			return Kind::Bool;
		case Takes::Text:
		case Takes::Words:
			return Kind::Str;
		case Takes::Any:
			return Kind::Any;
		case Takes::Element:
			return p_element;
		case Takes::Own:
		case Takes::Object:
			return Kind::Obj;
		default:
			return Kind::Void;
	}
}

} // namespace ir
} // namespace kas3
