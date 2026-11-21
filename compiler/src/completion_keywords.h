// The keywords offered in each completion context, in display order.
#pragma once

#include <string_view>

namespace kas3::completion_keywords {

struct Keyword {
	std::string_view name;
	std::string_view documentation;
};

constexpr Keyword KW_VAR{ "var", "Declares a variable with an optional type and initial value." };
constexpr Keyword KW_CONST{ "const", "Declares a value that can be assigned only once." };
constexpr Keyword KW_FUNCTION{ "function", "Declares a function or introduces a function expression." };
constexpr Keyword KW_PUBLIC{ "public", "Makes a declaration accessible wherever its containing type or package is visible." };
constexpr Keyword KW_INTERNAL{ "internal", "Restricts access to the declaring package." };
constexpr Keyword KW_FINAL{ "final", "Prevents subclassing a class or overriding a member." };
constexpr Keyword KW_IMPORT{ "import", "Makes a definition or a package's public definitions available by short name." };

constexpr Keyword IN_FUNCTION[] = {
	KW_VAR,
	KW_CONST,
	{ "if", "Runs a statement or block when its condition is true." },
	{ "else", "Runs the alternative branch when the preceding if condition is false." },
	{ "for", "Repeats a statement with initialization, a condition, and an update. Use for ... in to visit keys." },
	{ "for each", "Iterates over the values of a collection." },
	{ "each", "Use after for to iterate over values, as in for each (var value:Object in items)." },
	{ "in", "Tests whether a key exists, or introduces the collection in a for ... in loop." },
	{ "while", "Repeats a statement while its condition is true." },
	{ "do", "Runs a loop body once before testing the while condition." },
	{ "return", "Ends the current function and optionally returns a value." },
	{ "break", "Exits the enclosing loop or switch, or jumps to the end of a labeled statement." },
	{ "continue", "Starts the next iteration of a loop." },
	{ "switch", "Selects a case by comparing its value with the switch expression." },
	{ "case", "Marks a possible match in a switch statement." },
	{ "default", "Marks the branch taken when no switch case matches." },
	{ "new", "Constructs an instance using the supplied constructor arguments." },
	{ "null", "An explicit empty object reference." },
	{ "true", "The Boolean value true." },
	{ "false", "The Boolean value false." },
	{ "is", "Tests whether a value is an instance of the specified type." },
	{ "as", "Attempts a type cast. Returns null when a reference cannot be cast." },
	{ "typeof", "Returns a string identifying the kind of a value." },
	KW_FUNCTION,
	{ "throw", "Raises an exception with the supplied value." },
	{ "try", "Runs a block with a catch handler, a finally block, or both." },
	{ "catch", "Handles a matching exception thrown by the preceding try block." },
	{ "finally", "Runs a cleanup block when control leaves the try or catch block." },
	{ "super", "Refers to the base class implementation. Call super(...) to invoke its constructor." },
	{ "delete", "Removes a dynamic property by name or index." },
	{ "void", "Declares a function with no return value. In an expression, void evaluates its operand and yields undefined." },
};

constexpr Keyword IN_CLASS[] = {
	KW_PUBLIC,
	{ "private", "Restricts access to the declaring class." },
	{ "protected", "Allows access from the declaring class and its subclasses." },
	KW_INTERNAL,
	{ "static", "Declares a member on the class itself." },
	{ "override", "Replaces an inherited method or accessor with a matching declaration." },
	KW_FINAL,
	KW_FUNCTION,
	KW_VAR,
	KW_CONST,
	{ "native", "Declares a class or a bodyless function supplied by the runtime in a declaration file." },
	KW_IMPORT,
	{ "get", "Declares the accessor that runs when a property is read." },
	{ "set", "Declares the accessor that runs when a property is assigned." },
};

constexpr Keyword IN_FILE[] = {
	{ "package", "Groups definitions under a shared package name." },
	KW_IMPORT,
	KW_PUBLIC,
	KW_INTERNAL,
	{ "class", "Declares a type with fields, methods, and an optional constructor." },
	{ "interface", "Declares the method and accessor signatures implementing classes must provide." },
	{ "extends", "Specifies a class's base class or an interface's parent interfaces." },
	{ "implements", "Lists the interfaces a class agrees to implement." },
	KW_FUNCTION,
	KW_VAR,
	KW_CONST,
	KW_FINAL,
	{ "dynamic", "Marks a built-in class that can hold dynamic properties. User-defined dynamic classes are unsupported." },
};

} // namespace kas3::completion_keywords
