#include "program_writer.h"

#include "function_writer.h"
#include "names.h"

#include <cstdio>

namespace kas3 {
namespace backend_cpp {

using ir::Kind;
using ir::NONE;

namespace {

constexpr uint32_t HEADER_BYTES = 32; // sizeof(vm::Object): the header every object starts with.
constexpr uint32_t SLOT_BYTES = 8;

std::string boolean(bool p_value) {
	return p_value ? "true" : "false";
}

std::string code_pointer(const ir::Module &p_module, uint32_t p_function) {
	return p_function == NONE ? std::string("nullptr") : "reinterpret_cast<Code>(&" + function_name(p_module, p_function) + ")";
}

// A table of the items, named, written into r_out; what refers to it: the name, or `nullptr` for none
// (C++ has no array of nothing).
std::string array(std::string &r_out, const std::string &p_type, const std::string &p_name, const std::vector<std::string> &p_items) {
	if (p_items.empty()) {
		return "nullptr";
	}
	r_out += p_type + " " + p_name + "[] = {";
	for (const std::string &item : p_items) {
		r_out += " " + item + ",";
	}
	r_out += " };\n";
	return p_name;
}

std::vector<std::string> numbers(const std::vector<uint32_t> &p_values) {
	std::vector<std::string> out;
	for (uint32_t value : p_values) {
		out.push_back(number(value));
	}
	return out;
}

std::string kind_number(Kind p_kind) {
	return std::to_string(static_cast<unsigned>(p_kind));
}

// FNV-1a over what decides where an object's fields are and what they hold.
std::string layout_hash(const ir::Class &p_class) {
	uint64_t hash = 0xcbf29ce484222325ull;
	const auto mix = [&](std::string_view p_bytes) {
		for (char each : p_bytes) {
			hash = (hash ^ static_cast<unsigned char>(each)) * 0x100000001b3ull;
		}
	};
	for (size_t i = 0; i < p_class.fields.size(); i++) {
		mix(p_class.fields[i].name);
		mix(std::string(1, '\0') + kind_number(p_class.fields[i].kind) + ":" + std::to_string(HEADER_BYTES + SLOT_BYTES * i) + ";");
	}
	char text[32];
	std::snprintf(text, sizeof(text), "0x%016llxull", static_cast<unsigned long long>(hash));
	return text;
}

std::string class_tables(const ir::Module &p_module, uint32_t p_index, std::string &r_out) {
	const ir::Class &klass = p_module.classes[p_index];
	const std::string prefix = "C" + std::to_string(p_index);
	std::vector<std::string> fields, code, named, interface_code;
	for (size_t i = 0; i < klass.fields.size(); i++) {
		const ir::Field &field = klass.fields[i];
		fields.push_back("{ " + string_literal(field.name) + ", " + kind_number(field.kind) + ", " + number(field.klass) + ", " +
				std::to_string(HEADER_BYTES + SLOT_BYTES * i) + ", " + boolean(field.starts_null) + ", " + boolean(field.is_public) + ", " +
				boolean(field.is_protected) + ", " + boolean(field.constant) + " }");
	}
	for (uint32_t method : klass.methods) {
		code.push_back(code_pointer(p_module, method));
	}
	for (const auto &[name, slot] : klass.named_methods) {
		named.push_back("{ " + string_literal(name) + ", " + std::to_string(slot) + " }");
	}
	// As long as the program's list, whatever the class implements: a call by interface reads its entry
	// with no look at the table's length, and finds nothing where the class has none.
	for (size_t method = 0; method < p_module.interface_methods.size(); method++) {
		interface_code.push_back(code_pointer(p_module, method < klass.interface_methods.size() ? klass.interface_methods[method] : NONE));
	}
	std::string info = "{ " + string_literal(klass.name) + ", " + number(klass.base) + ", " + layout_hash(klass) + ", " + kind_number(klass.element) + ", " + number(klass.element_check) + ", " +
			std::to_string(static_cast<unsigned>(klass.element_bare)) + ", " +
			boolean(klass.array) + ", " + boolean(klass.table) + ", " + boolean(klass.dictionary) + ", " + boolean(klass.built_in) + ", " +
			boolean(klass.is_interface) + ", ";
	info += array(r_out, "const FieldInfo", prefix + "_fields", fields) + ", " + std::to_string(fields.size()) + ", ";
	info += array(r_out, "const uint32_t", prefix + "_methods", numbers(klass.methods)) + ", ";
	info += array(r_out, "const Code", prefix + "_code", code) + ", " + std::to_string(code.size()) + ", ";
	info += array(r_out, "const NamedMethod", prefix + "_named", named) + ", " + std::to_string(named.size()) + ", ";
	info += number(klass.constructor) + ", " + number(klass.static_init) + ", " + number(klass.to_string) + ", ";
	info += array(r_out, "const uint32_t", prefix + "_interfaces", numbers(klass.interfaces)) + ", " + std::to_string(klass.interfaces.size()) + ", ";
	info += array(r_out, "const uint32_t", prefix + "_interface_methods", numbers(klass.interface_methods)) + ", ";
	info += array(r_out, "const Code", prefix + "_interface_code", interface_code) + ", " + std::to_string(klass.interface_methods.size()) + " }";
	return info;
}

std::string function_tables(const ir::Module &p_module, uint32_t p_index, std::string &r_out) {
	const ir::Function &function = p_module.functions[p_index];
	const std::string prefix = "F" + std::to_string(p_index);
	std::vector<std::string> kinds, classes;
	for (uint32_t i = 0; i < function.parameters; i++) {
		kinds.push_back(kind_number(function.registers[i]));
		classes.push_back(number(i < function.parameter_classes.size() ? function.parameter_classes[i] : NONE));
	}
	const uint32_t starts = line_of(p_module, function.file, function.code.empty() ? function.position : function.code[0].position);
	std::string info = "{ " + string_literal(function.name) + ", " + std::to_string(function.file) + ", " + std::to_string(starts) + ", " +
			number(function.owner) + ", " +
			number(function.written_in) + ", " + boolean(function.is_static) + ", " + boolean(function.is_public) + ", " + boolean(function.rest) + ", " +
			boolean(function.bound_engine_method) + ", " + boolean(function.takes_extra) + ", " +
			kind_number(function.result) + ", " + std::to_string(function.parameters) + ", " + std::to_string(function.required) + ", ";
	info += array(r_out, "const uint8_t", prefix + "_kinds", kinds) + ", ";
	info += array(r_out, "const uint32_t", prefix + "_classes", classes) + ", ";
	return info + "&" + entry_name(p_index) + ", " + code_pointer(p_module, p_index) + " }";
}

std::string thunk_tables(const Thunk &p_thunk, size_t p_index, std::string &r_out) {
	const std::string prefix = "T" + std::to_string(p_index);
	std::vector<std::string> kinds;
	for (Kind kind : p_thunk.kinds) {
		kinds.push_back(kind_number(kind));
	}
	std::string info = "{ " + std::to_string(static_cast<unsigned>(p_thunk.op)) + ", " + number(p_thunk.a) + ", " + number(p_thunk.b) + ", " +
			number(p_thunk.c) + ", ";
	info += array(r_out, "const uint8_t", prefix + "_kinds", kinds) + ", " + std::to_string(kinds.size()) + ", " + std::to_string(p_thunk.given) + ", ";
	std::vector<std::string> types;
	bool typed = false;
	for (size_t i = 0; i < p_thunk.kinds.size(); i++) {
		const bool named = i < p_thunk.types.size() && !p_thunk.types[i].empty();
		types.push_back(named ? string_literal(p_thunk.types[i]) : std::string("nullptr"));
		typed = typed || named;
	}
	info += (typed ? array(r_out, "const char *const", prefix + "_types", types) : std::string("nullptr")) + ", ";
	info += array(r_out, "const uint32_t", prefix + "_list", numbers(p_thunk.list)) + ", " + std::to_string(p_thunk.list.size()) + ", ";
	return info + number(p_thunk.result) + ", " + number(p_thunk.updated) + ", " + number(p_thunk.code_class) + ", " + number(p_thunk.self) + " }";
}

std::string known_classes(const ir::Module &p_module) {
	std::string out = "{";
	for (uint32_t each : { p_module.entry_class, p_module.error_class, p_module.type_error_class, p_module.range_error_class,
				 p_module.argument_error_class, p_module.function_class, p_module.array_class, p_module.object_class, p_module.dictionary_class,
				 p_module.reference_error_class, p_module.regexp_class, p_module.date_class, p_module.syntax_error_class, p_module.uri_error_class,
				 p_module.class_class }) {
		out += " " + number(each) + ",";
	}
	return out + " }";
}

} // namespace

std::string generated_notice(std::string_view p_from) {
	return "// Generated by `kas3c cpp` from " + std::string(p_from) + ". Do not edit.\n";
}

bool has_struct(const ir::Class &p_class) {
	return p_class.element == Kind::Void && !p_class.table && !p_class.is_interface;
}

std::string write_header(const ir::Module &p_module) {
	std::string out = generated_notice("the whole program") + "#pragma once\n\n// The runtime's functions are reached through the host's table, never as symbols.\n#define KAS3_PROGRAM_LIBRARY 1\n#include \"kas3.h\"\n\n#include <cmath>\n#include <cstdint>\n#include <limits>\n\n";
	out += "namespace " + std::string(PROGRAM_NAMESPACE) + " {\n\nusing namespace kas3::rt;\n\n";
	for (uint32_t i = 0; i < p_module.classes.size(); i++) {
		const ir::Class &klass = p_module.classes[i];
		if (!has_struct(klass)) {
			continue;
		}
		const size_t inherited = klass.base == NONE ? 0 : p_module.classes[klass.base].fields.size();
		const std::string name = class_name(p_module, i);
		out += "struct " + name + " : " + (klass.base == NONE ? std::string("Object") : class_name(p_module, klass.base)) + " {\n";
		for (size_t field = inherited; field < klass.fields.size(); field++) {
			out += "\talignas(8) " + declared(klass.fields[field].kind == Kind::F32 ? Kind::F64 : klass.fields[field].kind, field_name(klass, static_cast<uint32_t>(field))) + ";\n";
		}
		out += "};\nstatic_assert(sizeof(" + name + ") == sizeof(Object) + " + std::to_string(SLOT_BYTES * klass.fields.size()) +
				", \"a field takes one slot, after the header\");\n\n";
	}
	for (uint32_t i = 0; i < p_module.functions.size(); i++) {
		const Signature signature = signature_of(p_module.functions[i]);
		out += declared(p_module.functions[i].result, function_name(p_module, i)) + "(";
		for (size_t each = 0; each < signature.parameters.size(); each++) {
			out += (each ? ", " : "") + signature.parameters[each];
		}
		out += ");\nvoid " + entry_name(i) + "(Context *c, const Slot *a, uint32_t n, Slot *r);\n";
	}
	return out + "\n} // namespace " + std::string(PROGRAM_NAMESPACE) + "\n";
}

std::string write_table(const ir::Module &p_module, const ThunkTable &p_thunks, bool p_keeps_stack, bool p_remembers) {
	std::string tables = "namespace {\n\n";
	std::vector<std::string> classes, functions, globals, strings, files, interface_methods, thunks;
	for (uint32_t i = 0; i < p_module.classes.size(); i++) {
		classes.push_back(class_tables(p_module, i, tables));
	}
	for (uint32_t i = 0; i < p_module.functions.size(); i++) {
		functions.push_back(function_tables(p_module, i, tables));
	}
	for (const ir::Global &global : p_module.globals) {
		globals.push_back("{ " + string_literal(global.name) + ", " + kind_number(global.kind) + ", " + number(global.klass) + ", " +
				boolean(global.starts_null) + ", " + boolean(global.is_public) + ", " + boolean(global.is_const) + " }");
	}
	for (const std::string &text : p_module.strings) {
		strings.push_back("{ " + string_literal(text) + ", " + std::to_string(text.size()) + " }");
	}
	for (const std::string &file : p_module.files) {
		files.push_back(string_literal(file));
	}
	for (const std::string &name : p_module.interface_methods) {
		interface_methods.push_back(string_literal(name));
	}
	for (size_t i = 0; i < p_thunks.all().size(); i++) {
		thunks.push_back(thunk_tables(p_thunks.all()[i], i, tables));
	}
	// By its name: the number is the one of the runtime's headers the library is built against.
	std::string program = "const Program PROGRAM = { RUNTIME_LAYOUT, ";
	const auto counted = [&](const std::string &p_type, const std::string &p_name, const std::vector<std::string> &p_items) {
		program += array(tables, p_type, p_name, p_items) + ", " + std::to_string(p_items.size()) + ", ";
	};
	counted("const char *const", "FILES", files);
	counted("const ClassInfo", "CLASSES", classes);
	counted("const FunctionInfo", "FUNCTIONS", functions);
	counted("const GlobalInfo", "GLOBALS", globals);
	counted("const Text", "STRINGS", strings);
	counted("const char *const", "INTERFACE_METHODS", interface_methods);
	counted("const uint32_t", "INITIALIZERS", numbers(p_module.initializers));
	counted("const ThunkInfo", "THUNKS", thunks);
	counted("const uint32_t", "USES", numbers(p_thunks.uses()));
	counted("const uint32_t", "NAMES", numbers(p_thunks.names()));
	program += std::to_string(ir::fingerprint(p_module)) + "ull, ";
	program += known_classes(p_module) + ", " + number(p_module.library_function) + ", " + boolean(p_keeps_stack);
	// Written only where it is so: a program that does not tell is the same text as it was, and what the
	// table does not say is false.
	program += std::string(p_remembers ? ", true" : "") + " };\n";
	tables += program + "\n} // namespace\n\n} // namespace " + std::string(PROGRAM_NAMESPACE) + "\n\n";
	tables += "extern \"C\" KAS3_EXPORT const kas3::rt::Program *kas3_program() {\n\treturn &" + std::string(PROGRAM_NAMESPACE) + "::PROGRAM;\n}\n";
	return tables;
}

} // namespace backend_cpp
} // namespace kas3
