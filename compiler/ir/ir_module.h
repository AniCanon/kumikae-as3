// A module: its classes, functions, globals and constants, all referring to each other by index.
#pragma once

#include "ir_ops.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kas3 {
namespace ir {

// A local variable or parameter by name, for the debugger.
struct Local {
	std::string name;
	uint32_t reg = 0;
};

// Where an error raised inside [start, end) of a function's code is caught: the error goes to register
// `reg` and the function carries on at `target`. `type` is what the catch takes: a class, one of the
// ANY_IS_ codes, or NONE for anything. A function's handlers are in order, the innermost first.
struct Handler {
	uint32_t start = 0;
	uint32_t end = 0;
	uint32_t target = 0;
	uint32_t reg = 0;
	uint32_t type = NONE;
	// The catch of a `finally`, which throws the error again: the trace it had is kept for that.
	bool rethrows = false;
};

// The type of a register that may hold any of Godot's values (what a global function that gives back
// anything gives, a null): Godot's own name for "any value".
inline constexpr std::string_view ANY_GODOT_TYPE = "Variant";

struct Function {
	std::string name; // "Main/update", "Main" for a constructor, "helper" for a package function.
	uint32_t file = 0; // An index into Module::files.
	uint32_t position = 0; // Of its name.
	uint32_t owner = NONE; // The class, for a method or constructor.
	// For a function written inside a method (owner NONE): the class whose code it is, which decides what
	// it sees of objects by name.
	uint32_t written_in = NONE;
	bool is_static = false; // No receiver: a static method or a package function.
	bool is_public = false; // A public method: Godot may call it by name.
	// The first registers are the parameters; an instance method's first parameter is `this`.
	uint32_t parameters = 0;
	uint32_t required = 0; // How many of them have no default: a caller must pass at least these.
	// `...rest`: the last parameter's register is an Array of every argument past the others. A caller
	// passes those untyped, after the others, and as many as it likes.
	bool rest = false;
	Kind result = Kind::Void;
	// Godot's name for the type of each register: for every one that holds one of Godot's values (Kind::Val:
	// "Vector2", "Callable", or ANY_GODOT_TYPE where any may be there), and for one that holds one of
	// Godot's objects where its class is known ("CharacterBody2D"). Empty for the others, and the vector may
	// be shorter than the registers. A backend reads it; a host checks what arrives against it.
	std::vector<std::string> engine_types;
	// For each parameter that holds a script object: the class it is declared to hold, by register (NONE
	// for the others; the vector may be shorter). A call through a function value checks what it passes.
	std::vector<uint32_t> parameter_classes;
	// For each CallVirtual on an object other than `this`: the instruction, and the class that declares the
	// method called (what runs is its method or a subclass's override). Not printed: run_once_changes reads it.
	std::vector<std::pair<uint32_t, uint32_t>> virtual_receivers;
	std::vector<Kind> registers;
	std::vector<Instruction> code;
	std::vector<std::vector<uint32_t>> argument_lists;
	std::vector<Local> locals;
	std::vector<Handler> handlers;
};

// A value written in the source as a constant, for a host to be told of: `Empty` is what a field holds
// before anything is written to it; `None` that the value is not a constant.
struct Constant {
	// Value: one of Godot's values made from numbers (`new Vector2(1, 2)`), its type's Godot name in `text`.
	enum class Form : uint8_t { None, Empty, Number, Boolean, Text, Null, Value };
	Form form = Form::None;
	double number = 0; // A Number, or a Boolean as 0 or 1.
	std::string text;
	std::vector<double> parts; // A Value's numbers.
};

struct Field {
	std::string name;
	Kind kind = Kind::Void;
	// For an object: the class it is declared to hold (a class of the program, or a vector's), or NONE
	// where that is not known. A reload keeps what a field held only if it is still of that class.
	uint32_t klass = NONE;
	bool starts_null = false; // An untyped field declared `Object`: it starts null, a `*` undefined.
	// Public: reached by its name as the program runs (`x.name` on an untyped value), and by Godot (`node.speed`),
	// which may read it and write it unless it is a const. Godot is told of no other.
	bool is_public = false;
	// Protected: reached by name from the code of the class that declares it and of its subclasses. A field
	// neither public nor protected is reached by name only from the code of the class that declares it.
	bool is_protected = false;
	bool constant = false;
	// Godot's name for its type, when it holds one of Godot's values or objects ("Vector2", "Node").
	std::string engine_type = {};
	// [Export]: Godot's inspector shows it, and a scene's value for it is given to it.
	bool exported = false;
	// An exported field's first value, as Godot is told it, to compare a scene's value with.
	Constant first = {};
};

struct Class {
	std::string name; // "game.Player"
	uint32_t base = NONE;
	// For a Vector.<T>, what its elements are; Void for every other class. A vector class has no fields,
	// no methods and no base, and its objects are vm::Vector.
	Kind element = Kind::Void;
	// For a vector of one of Godot's value types: Godot's name for it ("Vector2"). Each place a vector of
	// one with parts (Vector2, Color: not an array or a signal) is made or grows by starts at its zero, as
	// a C# array of structs does and as a local of that type does.
	std::string element_type;
	bool element_zero = false;
	// Array: the one class of every Array, a vector of untyped elements with Array's own rules (a read past
	// the end is undefined, a write past it grows the array, never fixed).
	bool array = false;
	// Object and flash.utils.Dictionary: objects that are vm::Table, keyed by text (a Dictionary also by the
	// identity of an object), with no fields.
	bool table = false;
	bool dictionary = false;
	// A class of the library (Error, Function): AS3 makes it dynamic, so a name it lacks is not one of
	// Flash's ReferenceErrors here but something the interpreter does not do yet.
	bool built_in = false;
	std::vector<Field> fields; // The base's first: a field keeps its slot in every subclass.
	std::vector<uint32_t> methods; // The method table: function indices, the base's slots first.
	// The public instance methods, by name, with their method-table slots: reached by name as the program
	// runs. The base's first; an override is listed once, under its base's slot.
	std::vector<std::pair<std::string, uint32_t>> named_methods;
	uint32_t constructor = NONE;
	uint32_t static_init = NONE; // The function that gives the class's static fields their values, if any.
	uint32_t to_string = NONE; // The class's toString(), its own or inherited: what converting to a String calls.
	// For a script class, one that extends a Godot class directly or through other script classes: Godot's
	// name for that class. Field 0 then holds the Godot object the script is attached to.
	std::string native_base;
	// A script class whose Godot object may be reference-counted (it extends RefCounted, a Resource, or its
	// base is Godot's Object, which a RefCounted can carry too): when the program lets go of an object of it,
	// the engine is asked whether it still holds the Godot object.
	bool counted = false;
	// The engine's virtual methods the class answers: Godot's name ("_physics_process") and the function.
	std::vector<std::pair<std::string, uint32_t>> virtuals;
	// An interface: it has no objects, fields, methods or constructor; a class lists it in `interfaces`.
	bool is_interface = false; // Not `interface`: a macro in Windows's headers.
	// Every interface the class implements, those its bases implement and those they extend; for an
	// interface, those it extends. `is`, `as` and every check of an object's class read it.
	std::vector<uint32_t> interfaces;
	// By interface method (Module::interface_methods): the function the class runs for it, or NONE.
	std::vector<uint32_t> interface_methods;
	// The signals the class declares itself (`[Signal] public var hit:Signal;`), by name: Godot is told of
	// them, with the base classes' too. Each one's field holds the signal from the start.
	std::vector<std::string> signals;

	// The field of this name (public only, if asked), the class's own before a base's it hides.
	std::optional<uint32_t> field(std::string_view p_name, bool p_public_only = false) const {
		for (size_t i = fields.size(); i-- > 0;) {
			if (fields[i].name == p_name && (fields[i].is_public || !p_public_only)) {
				return static_cast<uint32_t>(i);
			}
		}
		return std::nullopt;
	}
};

// One of Godot's classes, members, operators, global functions or defaults that the program names, as Godot's
// name table (godot/godot_names.txt) has it: what an engine instruction's E operand points at. Copied into
// the module so that the module stands alone; `id` says which entry of the table it was.
struct EngineName {
	uint32_t id = 0; // The entry's place in the name table the module was built with.
	std::string key; // "Node2D.position", "Vector2", "@operator.OP_ADD"
	std::string value; // The whole value: Godot's name, then what the table says of it ("position property ...").
	std::string name; // Godot's name: the value's first word ("position", "Vector2", "+"), or a default's text.
	std::string owner; // Godot's name for the class it is a member of ("Node2D"); empty for anything else.
};

struct Global {
	std::string name; // "Main.count"
	Kind kind = Kind::Void;
	uint32_t klass = NONE; // As for a field.
	bool starts_null = false; // As for a field.
};

struct Module {
	std::vector<std::string> files; // Paths, for stack traces.
	std::vector<std::vector<uint32_t>> lines; // Per file: the offset each line starts at.
	std::vector<Class> classes;
	std::vector<Function> functions;
	std::vector<Global> globals; // Static fields and package-level variables.
	std::vector<std::string> strings;
	std::vector<double> numbers;
	std::vector<int64_t> wides; // Int64 constants.
	std::vector<EngineName> engine_names; // What the engine instructions name, each once.
	// Functions run once, in order, before anything else: package-level variables. (A class's static fields
	// are set on the class's first use: Class::static_init.)
	std::vector<uint32_t> initializers;
	// The interfaces' methods by name, as AVM2 finds them: "area", "get name", "set name". One name is one
	// method, whichever interfaces declare it.
	std::vector<std::string> interface_methods;
	uint32_t entry_class = NONE; // Constructed with no arguments to start the program.
	uint32_t error_class = NONE; // Error, and TypeError: the classes of the errors the interpreter raises.
	uint32_t type_error_class = NONE;
	uint32_t range_error_class = NONE;
	uint32_t argument_error_class = NONE; // Made when a program calls a function value: too many arguments.
	// The built-in class of function values (FUNCTION_FIELDS), when the program has any.
	uint32_t function_class = NONE;
	uint32_t array_class = NONE; // The one class of every Array, when the program has any.
	uint32_t object_class = NONE; // The class of `{}` and `new Object()`, when the program makes one.
	uint32_t dictionary_class = NONE; // flash.utils.Dictionary, when the program makes one.
	// Made when a program reaches a property by name: a sealed object's missing one (#1069, #1056).
	uint32_t reference_error_class = NONE;
	// RegExp and Date, and the errors their functions and the URI functions raise, when the program uses them.
	uint32_t regexp_class = NONE;
	uint32_t date_class = NONE;
	uint32_t syntax_error_class = NONE;
	uint32_t uri_error_class = NONE;
};

} // namespace ir
} // namespace kas3
