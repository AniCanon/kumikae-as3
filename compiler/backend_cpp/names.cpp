#include "names.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kas3 {
namespace backend_cpp {

using ir::Kind;

std::string identifier(std::string_view p_name) {
	std::string out;
	for (char each : p_name) {
		const bool plain = (each >= 'a' && each <= 'z') || (each >= 'A' && each <= 'Z') || (each >= '0' && each <= '9');
		out += plain ? each : '_';
	}
	return out;
}

std::string function_name(const ir::Module &p_module, uint32_t p_function) {
	return "f" + std::to_string(p_function) + "_" + identifier(p_module.functions[p_function].name);
}

std::string entry_name(uint32_t p_function) {
	return "e" + std::to_string(p_function);
}

std::string class_name(const ir::Module &p_module, uint32_t p_class) {
	return "C" + std::to_string(p_class) + "_" + identifier(p_module.classes[p_class].name);
}

std::string field_name(const ir::Class &p_class, uint32_t p_field) {
	return "f" + std::to_string(p_field) + "_" + identifier(p_class.fields[p_field].name);
}

std::string register_name(uint32_t p_register) {
	return "r" + std::to_string(p_register);
}

std::string_view type_of(Kind p_kind) {
	switch (p_kind) {
		case Kind::I32:
			return "int32_t";
		case Kind::U32:
			return "uint32_t";
		case Kind::F32:
			return "float";
		case Kind::F64:
			return "double";
		case Kind::Bool:
			return "bool";
		case Kind::Str:
			return "Str *";
		case Kind::Obj:
			return "Object *";
		case Kind::I64:
			return "int64_t";
		case Kind::Any:
			return "Slot";
		case Kind::Val:
			return "Val";
		case Kind::Ref:
			return "Ref";
		default:
			return "void";
	}
}

std::string declared(Kind p_kind, std::string_view p_name) {
	const std::string_view type = type_of(p_kind);
	return std::string(type) + (type.back() == '*' ? "" : " ") + std::string(p_name);
}

std::string_view zero_of(Kind p_kind) {
	switch (p_kind) {
		case Kind::Bool:
			return "false";
		case Kind::Str:
		case Kind::Obj:
			return "nullptr";
		case Kind::Any:
			return "empty()";
		case Kind::Val:
			return "Val{}";
		case Kind::Ref:
			return "Ref{}";
		default:
			return "0";
	}
}

std::string_view slot_member(Kind p_kind) {
	switch (p_kind) {
		case Kind::I32:
			return "i";
		case Kind::U32:
			return "u";
		case Kind::F32:
		case Kind::F64:
			return "d";
		case Kind::Bool:
			return "b";
		case Kind::I64:
			return "l";
		default:
			return "p";
	}
}

std::string from_slot(Kind p_kind, std::string_view p_slot) {
	const std::string slot(p_slot);
	if (p_kind == Kind::Any) {
		return slot;
	}
	if (ir::is_engine(p_kind)) {
		return std::string(type_of(p_kind)) + "{ " + slot + ".p }";
	}
	if (p_kind == Kind::Str || p_kind == Kind::Obj) {
		return "static_cast<" + std::string(type_of(p_kind)) + ">(" + slot + ".p)";
	}
	return slot + "." + std::string(slot_member(p_kind));
}

bool has_parts(std::string_view p_godot_type) {
	// The structs of runtime/godot_values.h, which this folder can't include: a name that is not one there
	// does not compile.
	static constexpr std::string_view NAMES[] = { "Vector2", "Vector2i", "Rect2", "Rect2i", "Vector3", "Vector3i", "Transform2D", "Vector4",
		"Vector4i", "Plane", "Quaternion", "AABB", "Basis", "Transform3D", "Projection", "Color" };
	return std::find(std::begin(NAMES), std::end(NAMES), p_godot_type) != std::end(NAMES);
}

bool in_handle(std::string_view p_godot_type) {
	return p_godot_type == "Dictionary" || p_godot_type == "Array" || p_godot_type == "Callable" || p_godot_type == "RID";
}

std::string_view parts_of(const ir::Function &p_function, uint32_t p_register) {
	// A parameter is never held inline: a caller may give null, which a struct can't say.
	if (p_register < p_function.parameters || p_function.registers[p_register] != Kind::Val || p_register >= p_function.engine_types.size()) {
		return {};
	}
	const std::string_view type = p_function.engine_types[p_register];
	return has_parts(type) ? type : std::string_view();
}

std::vector<std::string_view> words_of(std::string_view p_text) {
	std::vector<std::string_view> words;
	for (size_t at = 0; at < p_text.size();) {
		const size_t space = std::min(p_text.find(' ', at), p_text.size());
		if (space > at) {
			words.push_back(p_text.substr(at, space - at));
		}
		at = space + 1;
	}
	return words;
}

std::string number_literal(double p_value) {
	if (std::isnan(p_value)) {
		return "std::numeric_limits<double>::quiet_NaN()";
	}
	if (std::isinf(p_value)) {
		return p_value < 0 ? "-std::numeric_limits<double>::infinity()" : "std::numeric_limits<double>::infinity()";
	}
	// A hexadecimal literal says every bit: no decimal text has to be the shortest that reads back.
	char text[64];
	std::snprintf(text, sizeof(text), "%a", p_value);
	return text;
}

std::string wide_literal(int64_t p_value) {
	// The least Int64 can't be written as a literal: `-9223372036854775808` is a minus and a number too big.
	if (p_value == INT64_MIN) {
		return "INT64_MIN";
	}
	return "INT64_C(" + std::to_string(p_value) + ")";
}

std::string string_literal(std::string_view p_text) {
	std::string out = "\"";
	for (char each : p_text) {
		const unsigned char byte = static_cast<unsigned char>(each);
		if (each == '"' || each == '\\') {
			out += '\\';
			out += each;
		} else if (byte >= 0x20 && byte < 0x7f && each != '?') {
			out += each; // `?` is written by number: two in a row could start a trigraph.
		} else {
			// Three octal digits, always: a digit that follows is then never taken into the escape.
			char escape[8];
			std::snprintf(escape, sizeof(escape), "\\%03o", static_cast<unsigned>(byte));
			out += escape;
		}
	}
	return out + "\"";
}

std::string number(uint32_t p_value) {
	return p_value == ir::NONE ? "NONE" : std::to_string(p_value);
}

uint32_t line_of(const ir::Module &p_module, uint32_t p_file, uint32_t p_position) {
	if (p_file >= p_module.lines.size()) {
		return 0;
	}
	const std::vector<uint32_t> &lines = p_module.lines[p_file];
	return static_cast<uint32_t>(std::upper_bound(lines.begin(), lines.end(), p_position) - lines.begin());
}

} // namespace backend_cpp
} // namespace kas3
