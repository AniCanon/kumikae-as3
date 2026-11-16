// What a program library gives its host: one table, from the one exported function `kas3_program`. The
// host reads names, kinds and offsets from it and never a C++ type; everything refers to everything else by
// number, as in the IR's module (ir/ir_module.h), and the numbers are the module's.
#pragma once

#include "values.h"

#include <cstddef>
#include <cstdint>

namespace kas3 {
namespace rt {

constexpr uint32_t NONE = 0xFFFFFFFFu;

struct Text {
	const char *data;
	size_t size;
};

// A generated function, whatever it takes and gives: what calls it casts it to its own type.
using Code = void (*)();

// A field of a class: the kind is an ir::Kind.
struct FieldInfo {
	const char *name;
	uint8_t kind;
	uint32_t klass; // For an object: the class it is declared to hold, or NONE.
	uint32_t offset; // From the start of the object, in bytes.
	bool starts_null;
	bool is_public;
	bool is_protected;
	bool constant;
};

struct NamedMethod {
	const char *name;
	uint32_t slot;
};

struct ClassInfo {
	const char *name; // "game.Player"
	uint32_t base;
	uint64_t layout; // A hash of the fields' names, kinds and offsets: equal layouts hold objects alike.
	uint8_t element; // For a Vector.<T>: the ir::Kind of its elements. Void (0) for every other class.
	uint32_t element_check; // What a value stored into such a vector is checked against (ir::Class::element_check).
	bool array;
	bool table;
	bool dictionary;
	bool built_in;
	bool is_interface;
	const FieldInfo *fields; // The base's first.
	uint32_t field_count;
	const uint32_t *methods; // The method table: a function for each method number, the base's first.
	const Code *code; // The same table as the functions themselves.
	uint32_t method_count;
	const NamedMethod *named_methods;
	uint32_t named_method_count;
	uint32_t constructor;
	uint32_t static_init;
	uint32_t to_string;
	const uint32_t *interfaces;
	uint32_t interface_count;
	const uint32_t *interface_methods; // By interface method: the function the class runs for it, or NONE.
	// The same as the functions themselves, null where there is none: one entry for every interface method
	// of the program, not only for those the class's own list reaches.
	const Code *interface_code;
	uint32_t interface_method_count;
};

// A function called with its arguments as slots, each in its parameter's kind: how the host, and code that
// knows a function only by its number, calls one. There is a slot for every parameter; p_count is how many
// the caller gave, and the rest (empty) take their defaults. A result is the caller's to release.
using Entry = void (*)(Context *c, const Slot *p_arguments, uint32_t p_count, Slot *r_result);

struct FunctionInfo {
	const char *name; // "Main/update", "Main" for a constructor, "helper" for a package function.
	uint32_t file;
	uint32_t line; // Where its code starts: the line a stack trace gives a function that had not begun.
	uint32_t owner;
	uint32_t written_in;
	bool is_static;
	bool is_public;
	bool rest;
	uint8_t result; // An ir::Kind.
	uint32_t parameters;
	uint32_t required;
	const uint8_t *kinds; // The parameters' kinds.
	const uint32_t *classes; // The class each parameter is declared to hold, or NONE.
	Entry entry;
	Code direct; // The function itself.
};

struct GlobalInfo {
	const char *name; // "Main.count"
	uint8_t kind;
	uint32_t klass;
	bool starts_null;
};

// An operation the host carries out with the library it shares with the interpreter (runtime/thunks.h):
// the instruction, with each register operand numbered among the thunk's own registers.
struct ThunkInfo {
	uint8_t op; // An ir::Op.
	uint32_t a, b, c;
	const uint8_t *kinds; // Its registers' kinds: the values handed in first, then what it writes.
	uint32_t registers;
	uint32_t given; // How many are handed in.
	const uint32_t *list; // The instruction's argument list, if it takes one.
	uint32_t list_size;
	uint32_t result; // The register handed back, or NONE.
	uint32_t updated; // A register handed in that the operation moves on, handed back too; or NONE.
	uint32_t code_class; // The class whose code it is: what it may reach by name.
};

// The classes the library itself makes objects of, as the module names them (ir::Module).
struct KnownClasses {
	uint32_t entry, error, type_error, range_error, argument_error, function, array, object, dictionary, reference_error, regexp, date,
			syntax_error, uri_error, class_value;
};

struct Program {
	const char *const *files;
	uint32_t file_count;
	const ClassInfo *classes;
	uint32_t class_count;
	const FunctionInfo *functions;
	uint32_t function_count;
	const GlobalInfo *globals;
	uint32_t global_count;
	const Text *strings;
	uint32_t string_count;
	const char *const *interface_methods;
	uint32_t interface_method_count;
	const uint32_t *initializers; // Functions run once, in order, before anything else.
	uint32_t initializer_count;
	const ThunkInfo *thunks;
	uint32_t thunk_count;
	KnownClasses known;
	// The function behind a method of the library read by name (ir::Module::library_function), or NONE.
	uint32_t library_function;
	bool keeps_stack; // Its functions keep a StackFrame each: it reads an Error's getStackTrace().
};

} // namespace rt
} // namespace kas3

// The one symbol a program library exports: built with hidden visibility, it shows nothing else.
#if defined(_WIN32)
#define KAS3_EXPORT __declspec(dllexport)
#else
#define KAS3_EXPORT __attribute__((visibility("default")))
#endif
extern "C" KAS3_EXPORT const kas3::rt::Program *kas3_program();
