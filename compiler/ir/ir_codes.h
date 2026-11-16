// What the codes in an instruction's operands mean, for the operations that take one: which of Math's,
// a vector's, String's, RegExp's, Date's or JSON's functions, and the fields of the library's objects.
#pragma once

#include <cstdint>

namespace kas3 {
namespace ir {

// The functions of Math that take one Number, in MathUnary's third operand.
enum class MathFunction : uint8_t { Abs, Acos, Asin, Atan, Ceil, Cos, Exp, Floor, Log, Round, Sin, Sqrt, Tan };

// The fields of a function value: the function, and what it is called with first (the object of a method,
// or the environment of a function expression), or null for a function that takes nothing first.
constexpr uint32_t FUNCTION_INDEX = 0, FUNCTION_FIRST = 1;

// The fields of Error and of its built-in subclasses, which the interpreter fills in for an error it raises.
constexpr uint32_t ERROR_MESSAGE = 0, ERROR_NAME = 1, ERROR_ID = 2, ERROR_STACK = 3;

// A vector's methods, in VecCall's second operand. After the vector, the arguments are:
//   Push, Unshift   elements                         -> the new length (uint)
//   Pop, Shift      nothing                          -> the element
//   IndexOf, LastIndexOf   an element, where to start (int)   -> the index (int), or -1
//   Reverse         nothing                          -> the vector itself
//   Slice           start, end (int)                 -> a new vector
//   Concat          vectors of the same class        -> a new vector
//   Join            the separator (String)           -> a String
//   InsertAt        index (int), an element          -> nothing
//   RemoveAt        index (int)                      -> the element
//   Splice          start (int), how many (uint), elements to put in   -> a new vector of what was removed
//   Sort            a function that compares two elements, or the options as a number (untyped)   -> the vector
// (forEach, map, filter, some and every are loops the lowerer writes, calling the function each time.)
//   SortOn          the field names, the options (untyped), as Array's sortOn takes them   -> as Sort's
enum class VectorMethod : uint8_t { Push, Unshift, Pop, Shift, IndexOf, LastIndexOf, Reverse, Slice, Concat, Join, InsertAt, RemoveAt, Splice, Sort, SortOn };

// String's methods and the functions on text, in StrCall's second operand. An index or a count is a
// Number as the program wrote it; text is a String, and a null one reads "null". After the string:
//   CharAt, CharCodeAt    an index                -> String, or a Number (NaN outside the string)
//   IndexOf, LastIndexOf  text, where to start    -> int
//   Substr                start, length           -> String
//   Substring, Slice      start, end              -> String
//   Concat                any number of texts     -> String
//   Upper, Lower          nothing                 -> String
//   Replace               text, replacement       -> String: the first match, the replacement as it is
//   LocaleCompare         text                    -> int: -1, 0 or 1, by UTF-16 units
// With no string first:
//   FromCharCode          any number of Numbers   -> String
//   ParseInt              text, radix (int)       -> Number
//   ParseFloat            text                    -> Number
enum class StringFunction : uint8_t {
	CharAt, CharCodeAt, IndexOf, LastIndexOf, Substr, Substring, Slice, Concat, Upper, Lower, Replace, LocaleCompare,
	FromCharCode, ParseInt, ParseFloat,
	Split, // The string, the delimiter (untyped: a RegExp, or what becomes text), the limit (a Number): an Array.
	// The text -> String. A decode of text that is not a URI's, or an encode of a half of a character
	// alone, is AS3's URIError #1052.
	Escape, Unescape, EncodeURI, EncodeURIComponent, DecodeURI, DecodeURIComponent
};

// RegExp's methods and properties, in RegExpCall's second operand. After the RegExp, or the text for
// String's methods:
//   New                   the source, the flags (each a String, or untyped: a RegExp source is copied)  -> the RegExp
//   Exec                  the text                                -> an Array, or null (untyped)
//   Test                  the text                                -> Boolean
//   Source ... Extended   nothing                                 -> String, or Boolean
//   Match                 the pattern (untyped: a RegExp, or what becomes one)   -> an Array, or null
//   Search                the pattern                             -> int
//   Replace               the pattern, the replacement (both untyped: a function is called)   -> String
enum class RegExpFunction : uint8_t { New, Exec, Test, Source, Global, IgnoreCase, Multiline, Dotall, Extended, Match, Search, Replace };
// The fields of a RegExp: the source, the flags (vm/regex.h), and lastIndex, which a program reads and
// writes as a field.
constexpr uint32_t REGEXP_SOURCE = 0, REGEXP_FLAGS = 1, REGEXP_LAST_INDEX = 2;

// Date's methods and properties, in DateCall's second operand as date_code(function, detail, utc). Every
// number is a Number. After the Date:
//   New        nothing (now), one untyped value (a time, a Date, or text to parse), or the parts from
//              the year, two to seven of them (with no Date first)   -> the Date
//   Parse      the text; UTC   the parts (with no Date first)        -> the time
//   Time, TimezoneOffset   nothing    -> Number;   SetTime   the time   -> the time
//   Get        nothing (detail: a DatePart)  -> Number
//   Set        the parts written, from the detail's on (setHours(h, m)) -> the time
//   Text       nothing (detail: a DateText)  -> String
enum class DateFunction : uint8_t { New, Parse, UTC, Time, SetTime, TimezoneOffset, Get, Set, Text };
enum class DatePart : uint8_t { FullYear, Month, Date, Day, Hours, Minutes, Seconds, Milliseconds };
enum class DateText : uint8_t { Full, Date, Time, Locale, LocaleDate, LocaleTime, UTC };
constexpr uint32_t date_code(DateFunction p_function, uint32_t p_detail = 0, bool p_utc = false) {
	return static_cast<uint32_t>(p_function) | p_detail << 8 | (p_utc ? 1u << 16 : 0u);
}
constexpr uint32_t DATE_TIME = 0; // The one field of a Date: its time, in milliseconds since 1970 in UTC.

// JSON's functions, in JsonCall's second operand:
//   Parse      the text (String), the reviver (a Function, or null)          -> untyped: null for undefined
//   Stringify  the value, the replacer, the space (each untyped)             -> String: null for nothing
//   ToJson     a Dictionary, the key (String): Dictionary's toJSON()          -> untyped
enum class JsonFunction : uint8_t { Parse, Stringify, ToJson };

// toString(radix), toFixed, toExponential and toPrecision, in NumText's second operand. The number is a
// Number, or an Int64 (its register says which), written from its own digits.
enum class NumberFormat : uint8_t { Radix, Fixed, Exponential, Precision };

} // namespace ir
} // namespace kas3
