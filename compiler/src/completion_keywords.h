// The keywords offered in each completion context, in display order.
#pragma once

#include "token.h"

namespace kas3::completion_keywords {

struct Keyword {
	std::string_view name;
	std::string_view documentation;

	constexpr Keyword(std::string_view p_name, std::string_view p_documentation) :
			name(p_name), documentation(p_documentation) {}
	constexpr Keyword(TokenKind p_kind, std::string_view p_documentation) :
			Keyword(token_kind_spelling(p_kind), p_documentation) {}
};

inline constexpr Keyword KW_VAR{ TokenKind::KwVar, "Declares a variable with an optional type and initial value." };
inline constexpr Keyword KW_CONST{ TokenKind::KwConst, "Declares a value that can be assigned only once." };
inline constexpr Keyword KW_FUNCTION{ TokenKind::KwFunction, "Declares a function or introduces a function expression." };
inline constexpr Keyword KW_PUBLIC{ TokenKind::KwPublic, "Makes a declaration accessible wherever its containing type or package is visible." };
inline constexpr Keyword KW_INTERNAL{ TokenKind::KwInternal, "Restricts access to the declaring package." };
inline constexpr Keyword KW_FINAL{ "final", "Prevents subclassing a class or overriding a member." };
inline constexpr Keyword KW_IMPORT{ TokenKind::KwImport, "Makes a definition or a package's public definitions available by short name." };

inline constexpr Keyword KW_THIS{ TokenKind::KwThis, "The current instance of the class." };

inline constexpr Keyword IN_FUNCTION[] = {
	KW_VAR,
	KW_CONST,
	{ TokenKind::KwIf, "Runs a statement or block when its condition is true." },
	{ TokenKind::KwElse, "Runs the alternative branch when the preceding if condition is false." },
	{ TokenKind::KwFor, "Repeats a statement with initialization, a condition, and an update. Use for ... in to visit keys." },
	{ "for each", "Iterates over the values of a collection." },
	{ "each", "Use after for to iterate over values, as in for each (var value:Object in items)." },
	{ TokenKind::KwIn, "Tests whether a key exists, or introduces the collection in a for ... in loop." },
	{ TokenKind::KwWhile, "Repeats a statement while its condition is true." },
	{ TokenKind::KwDo, "Runs a loop body once before testing the while condition." },
	{ TokenKind::KwReturn, "Ends the current function and optionally returns a value." },
	{ TokenKind::KwBreak, "Exits the enclosing loop or switch, or jumps to the end of a labeled statement." },
	{ TokenKind::KwContinue, "Starts the next iteration of a loop." },
	{ TokenKind::KwSwitch, "Selects a case by comparing its value with the switch expression." },
	{ TokenKind::KwCase, "Marks a possible match in a switch statement." },
	{ TokenKind::KwDefault, "Marks the branch taken when no switch case matches." },
	{ TokenKind::KwNew, "Constructs an instance using the supplied constructor arguments." },
	{ TokenKind::KwNull, "An explicit empty object reference." },
	{ TokenKind::KwTrue, "The Boolean value true." },
	{ TokenKind::KwFalse, "The Boolean value false." },
	{ TokenKind::KwIs, "Tests whether a value is an instance of the specified type." },
	{ TokenKind::KwAs, "Attempts a type cast. Returns null when a reference cannot be cast." },
	{ TokenKind::KwTypeof, "Returns a string identifying the kind of a value." },
	KW_FUNCTION,
	{ TokenKind::KwThrow, "Raises an exception with the supplied value." },
	{ TokenKind::KwTry, "Runs a block with a catch handler, a finally block, or both." },
	{ TokenKind::KwCatch, "Handles a matching exception thrown by the preceding try block." },
	{ TokenKind::KwFinally, "Runs a cleanup block when control leaves the try or catch block." },
	{ TokenKind::KwSuper, "Refers to the base class implementation. Call super(...) to invoke its constructor." },
	{ TokenKind::KwDelete, "Removes a dynamic property by name or index." },
	{ TokenKind::KwVoid, "Declares a function with no return value. In an expression, void evaluates its operand and yields undefined." },
};

inline constexpr Keyword IN_CLASS[] = {
	KW_PUBLIC,
	{ TokenKind::KwPrivate, "Restricts access to the declaring class." },
	{ TokenKind::KwProtected, "Allows access from the declaring class and its subclasses." },
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

inline constexpr Keyword IN_FILE[] = {
	{ TokenKind::KwPackage, "Groups definitions under a shared package name." },
	KW_IMPORT,
	KW_PUBLIC,
	KW_INTERNAL,
	{ TokenKind::KwClass, "Declares a type with fields, methods, and an optional constructor." },
	{ TokenKind::KwInterface, "Declares the method and accessor signatures implementing classes must provide." },
	{ TokenKind::KwExtends, "Specifies a class's base class or an interface's parent interfaces." },
	{ TokenKind::KwImplements, "Lists the interfaces a class agrees to implement." },
	KW_FUNCTION,
	KW_VAR,
	KW_CONST,
	KW_FINAL,
	{ "dynamic", "Marks a built-in class that can hold dynamic properties. User-defined dynamic classes are unsupported." },
};

} // namespace kas3::completion_keywords
