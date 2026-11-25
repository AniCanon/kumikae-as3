// How the things of a module are written in C++: identifiers, types and literals.
#pragma once

#include "ir.h"

#include <string>
#include <string_view>
#include <vector>

namespace kas3 {
namespace backend_cpp {

// The namespace every generated definition is in. It opens with `using namespace kas3::rt`.
constexpr std::string_view PROGRAM_NAMESPACE = "kas3_generated";

// A name as part of a C++ identifier: letters, digits and `_`, anything else `_`.
std::string identifier(std::string_view p_name);
// "f12_Player_update": the number keeps two functions of one name apart.
std::string function_name(const ir::Module &p_module, uint32_t p_function);
std::string entry_name(uint32_t p_function); // The function called with its arguments as slots.
std::string class_name(const ir::Module &p_module, uint32_t p_class); // "C3_game_Player"
std::string field_name(const ir::Class &p_class, uint32_t p_field); // "f0_speed"
std::string register_name(uint32_t p_register); // "r4"

// The C++ type a register, field or result of the kind is held as.
std::string_view type_of(ir::Kind p_kind);
// A variable or parameter of the kind: "double r1", "Str *r2".
std::string declared(ir::Kind p_kind, std::string_view p_name);
// What a register of the kind holds before anything is written to it, as a C++ expression.
std::string_view zero_of(ir::Kind p_kind);
// The member of a slot (vm::Slot) a value of the kind is read from: "d" for a Number.
std::string_view slot_member(ir::Kind p_kind);
// A slot's value as the kind's C++ type: "static_cast<Str *>(x.p)" for a String.
std::string from_slot(ir::Kind p_kind, std::string_view p_slot);

// Whether one of Godot's types is a value with parts, which generated code holds inline as the runtime's
// struct of the same name (Vector2, Color); and the type a register holds that way, or nothing.
bool has_parts(std::string_view p_godot_type);
std::string_view parts_of(const ir::Function &p_function, uint32_t p_register);
// Whether one of Godot's types is a value that a handle keeps where generated code can reach it: a register
// holds the handle, a method bind gives such a value into a new handle (EngineApi::call_kept) and a method
// of the type is called where the handle keeps the value (value_in()). Only types that every handle on
// one value shares (a Dictionary, an Array) or that no method changes (a Callable, a RID): a method called
// where one handle keeps the value is then the method called through any other. The host's list is the
// same (godot_ext/native_program.cpp).
bool in_handle(std::string_view p_godot_type);
// The words of an engine name's value ("position property Vector2 get_position 334 set_position 743 -").
std::vector<std::string_view> words_of(std::string_view p_text, char p_separator = ' ');
// Parameter types in a method signature, which uses a dash for an empty list.
std::vector<std::string_view> parameter_types(std::string_view p_list);

// A Number as C++ reads back the very same one.
std::string number_literal(double p_value);
std::string wide_literal(int64_t p_value);
// Text as a C++ string literal, quotes included: every byte kept, whatever it is.
std::string string_literal(std::string_view p_text);
std::string number(uint32_t p_value); // "12", or "NONE" for ir::NONE.

// The line (from 1) a position of a file is on.
uint32_t line_of(const ir::Module &p_module, uint32_t p_file, uint32_t p_position);

} // namespace backend_cpp
} // namespace kas3
