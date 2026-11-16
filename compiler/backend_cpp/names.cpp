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
	if (p_kind == Kind::Str || p_kind == Kind::Obj) {
		return "static_cast<" + std::string(type_of(p_kind)) + ">(" + slot + ".p)";
	}
	return slot + "." + std::string(slot_member(p_kind));
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
