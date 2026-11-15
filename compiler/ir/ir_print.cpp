#include "ir.h"

#include <cmath>
#include <cstdio>

namespace kas3 {
namespace ir {

const char *kind_name(Kind p_kind) {
	switch (p_kind) {
		case Kind::Void:
			return "void";
		case Kind::I32:
			return "int";
		case Kind::U32:
			return "uint";
		case Kind::I64:
			return "Int64";
		case Kind::F64:
			return "Number";
		case Kind::Bool:
			return "Boolean";
		case Kind::Str:
			return "String";
		case Kind::Obj:
			return "Object";
		case Kind::Val:
			return "GodotValue";
		case Kind::Ref:
			return "GodotObject";
	}
	return "?";
}

const char *op_name(Op p_op) {
	switch (p_op) {
#define X(name, shape) \
	case Op::name: \
		return #name;
		KAS3_IR_OPS(X)
#undef X
	}
	return "?";
}

const char *op_shape(Op p_op) {
	switch (p_op) {
#define X(name, shape) \
	case Op::name: \
		return shape;
		KAS3_IR_OPS(X)
#undef X
	}
	return "---";
}

namespace {

std::string quoted(const std::string &p_text) {
	std::string out = "\"";
	for (char c : p_text) {
		if (c == '"' || c == '\\') {
			out += '\\';
			out += c;
		} else if (c == '\n') {
			out += "\\n";
		} else if (c == '\t') {
			out += "\\t";
		} else {
			out += c;
		}
	}
	return out + "\"";
}

std::string number(double p_value) {
	char buffer[40];
	std::snprintf(buffer, sizeof(buffer), "%.17g", p_value);
	return buffer;
}

std::string operand(const Module &p_module, const Function &p_function, char p_shape, uint32_t p_value) {
	switch (p_shape) {
		case 'R':
			return "r" + std::to_string(p_value);
		case 'r':
			return p_value == NONE ? "_" : "r" + std::to_string(p_value);
		case 'I':
			return std::to_string((int32_t)p_value);
		case 'W':
			return p_value < p_module.wides.size() ? std::to_string(p_module.wides[p_value]) : "?wide";
		case 'N':
			return p_value < p_module.numbers.size() ? number(p_module.numbers[p_value]) : "?number";
		case 'S':
			return p_value < p_module.strings.size() ? quoted(p_module.strings[p_value]) : "?string";
		case 'L':
			return "@" + std::to_string(p_value);
		case 'A': {
			std::string out = "(";
			if (p_value < p_function.argument_lists.size()) {
				const std::vector<uint32_t> &list = p_function.argument_lists[p_value];
				for (size_t i = 0; i < list.size(); i++) {
					out += (i ? ", r" : "r") + std::to_string(list[i]);
				}
			} else {
				out += "?";
			}
			return out + ")";
		}
		case 'F':
			return p_value < p_module.functions.size() ? p_module.functions[p_value].name : "?function";
		case 'C':
			return p_value < p_module.classes.size() ? p_module.classes[p_value].name : "?class";
		case 'D':
			return "." + std::to_string(p_value);
		case 'G':
			return p_value < p_module.globals.size() ? p_module.globals[p_value].name : "?global";
		case 'V':
			return "#" + std::to_string(p_value);
		default:
			return std::string();
	}
}

} // namespace

std::string print(const Module &p_module, const Function &p_function) {
	const Function &function = p_function;
	std::string out;
	out += std::string(function.is_static ? "static function " : "function ") + function.name + "(";
	for (uint32_t i = 0; i < function.parameters && i < function.registers.size(); i++) {
		out += (i ? ", r" : "r") + std::to_string(i) + ":" + kind_name(function.registers[i]);
	}
	out += std::string("):") + kind_name(function.result) + "\n";
	for (size_t i = 0; i < function.code.size(); i++) {
		const Instruction &instruction = function.code[i];
		if (instruction.op == Op::Stmt) {
			continue; // Markers only: they would drown the rest.
		}
		const char *shape = op_shape(instruction.op);
		out += "  @" + std::to_string(i) + " " + op_name(instruction.op);
		const uint32_t values[3] = { instruction.a, instruction.b, instruction.c };
		for (int n = 0; n < 3; n++) {
			if (shape[n] != '-') {
				out += " " + operand(p_module, function, shape[n], values[n]);
			}
		}
		out += "\n";
	}
	return out;
}

std::string print(const Module &p_module) {
	std::string out;
	for (const Class &klass : p_module.classes) {
		out += "class " + klass.name;
		if (klass.base != NONE && klass.base < p_module.classes.size()) {
			out += " extends " + p_module.classes[klass.base].name;
		}
		if (!klass.native_base.empty()) {
			out += " on Godot's " + klass.native_base;
		}
		if (klass.element != Kind::Void) {
			out += std::string(" of ") + kind_name(klass.element);
		}
		out += "\n";
		for (size_t i = 0; i < klass.fields.size(); i++) {
			out += "  ." + std::to_string(i) + " " + klass.fields[i].name + ":" + kind_name(klass.fields[i].kind) + "\n";
		}
		for (size_t i = 0; i < klass.methods.size(); i++) {
			out += "  #" + std::to_string(i) + " " +
					(klass.methods[i] < p_module.functions.size() ? p_module.functions[klass.methods[i]].name : "?") + "\n";
		}
		if (klass.static_init != NONE && klass.static_init < p_module.functions.size()) {
			out += "  static " + p_module.functions[klass.static_init].name + "\n";
		}
		for (const auto &entry : klass.virtuals) {
			out += "  virtual " + entry.first + " " + (entry.second < p_module.functions.size() ? p_module.functions[entry.second].name : "?") + "\n";
		}
		if (klass.constructor != NONE && klass.constructor < p_module.functions.size()) {
			out += "  new " + p_module.functions[klass.constructor].name + "\n";
		}
	}
	for (const Global &global : p_module.globals) {
		out += "global " + global.name + ":" + kind_name(global.kind) + "\n";
	}
	for (uint32_t initializer : p_module.initializers) {
		out += "initializer " + (initializer < p_module.functions.size() ? p_module.functions[initializer].name : "?") + "\n";
	}
	if (p_module.entry_class != NONE && p_module.entry_class < p_module.classes.size()) {
		out += "entry " + p_module.classes[p_module.entry_class].name + "\n";
	}
	for (const Function &function : p_module.functions) {
		out += print(p_module, function);
	}
	return out;
}

} // namespace ir
} // namespace kas3
