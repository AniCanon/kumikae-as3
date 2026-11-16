// The module the host's interpreter is given. Its classes are the program's, so that the library finds
// fields, methods and names as it would under the interpreter; its functions have no bodies of their own:
// each asks the bridge to run the generated function, and each thunk is one instruction.
#include "host.h"

namespace kas3 {
namespace rt {

using ir::Instruction;
using ir::Kind;
using ir::Op;

std::string parameter_local(uint32_t p_index) {
	return "p" + std::to_string(p_index);
}

std::string missing_local(uint32_t p_index) {
	return "m" + std::to_string(p_index);
}

namespace {

// The numbers of the bridge's names among the module's engine names: an engine instruction names what it asks for there.
struct Names {
	uint32_t call, caught, updated;
};

uint32_t add_register(ir::Function &r_function, Kind p_kind) {
	r_function.registers.push_back(p_kind);
	return static_cast<uint32_t>(r_function.registers.size() - 1);
}

// A call of the bridge that passes nothing: what it needs it reads from the frame, by the locals' names.
// (An untyped register passed to an engine call is refused if it holds a script object.)
Instruction bridge_call(ir::Function &r_function, uint32_t p_out, uint32_t p_name) {
	if (r_function.argument_lists.empty()) {
		r_function.argument_lists.emplace_back();
	}
	return Instruction{ Op::HostCallStatic, p_out, p_name, 0, 0 };
}

ir::Function stub(const FunctionInfo &p_info, const Names &p_names) {
	ir::Function function;
	function.name = p_info.name;
	function.file = p_info.file;
	function.owner = p_info.owner;
	function.written_in = p_info.written_in;
	function.is_static = p_info.is_static;
	function.is_public = p_info.is_public;
	function.parameters = p_info.parameters;
	function.required = p_info.required;
	function.rest = p_info.rest;
	function.result = static_cast<Kind>(p_info.result);
	for (uint32_t i = 0; i < p_info.parameters; i++) {
		function.registers.push_back(static_cast<Kind>(p_info.kinds[i]));
		function.parameter_classes.push_back(p_info.classes[i]);
		function.locals.push_back(ir::Local{ parameter_local(i), i });
	}
	const uint32_t fixed = p_info.rest ? p_info.parameters - 1 : p_info.parameters;
	for (uint32_t i = p_info.required; i < fixed; i++) {
		const uint32_t missing = add_register(function, Kind::Bool);
		function.code.push_back(Instruction{ Op::ArgMissing, missing, i, 0, 0 });
		function.locals.push_back(ir::Local{ missing_local(i), missing });
	}
	if (function.result == Kind::Void) {
		function.code.push_back(bridge_call(function, ir::NONE, p_names.call));
		function.code.push_back(Instruction{ Op::ReturnVoid, 0, 0, 0, 0 });
	} else {
		const uint32_t out = add_register(function, function.result);
		function.code.push_back(bridge_call(function, out, p_names.call));
		function.code.push_back(Instruction{ Op::Return, out, 0, 0, 0 });
	}
	return function;
}

// One instruction, then what it gives handed back. An error it raises is caught here and handed to the
// context: nothing leaves a thunk as an error of the interpreter's.
ir::Function thunk_function(const ThunkInfo &p_info, const Names &p_names) {
	ir::Function function;
	function.name = "kas3$thunk";
	function.is_static = true;
	function.written_in = p_info.code_class;
	function.parameters = p_info.given;
	function.required = p_info.given;
	for (uint32_t i = 0; i < p_info.registers; i++) {
		function.registers.push_back(static_cast<Kind>(p_info.kinds[i]));
	}
	function.argument_lists.emplace_back(); // The bridge's calls pass this one: nothing.
	Instruction in{ static_cast<Op>(p_info.op), p_info.a, p_info.b, p_info.c, 0 };
	const char *shape = ir::op_shape(in.op);
	uint32_t *operands[3] = { &in.a, &in.b, &in.c };
	for (int n = 0; n < 3; n++) {
		if (shape[n] == 'A') {
			*operands[n] = static_cast<uint32_t>(function.argument_lists.size());
			function.argument_lists.emplace_back(p_info.list, p_info.list + p_info.list_size);
		}
	}
	function.code.push_back(in);
	if (p_info.updated != NONE) {
		function.locals.push_back(ir::Local{ std::string(LOCAL_UPDATED), p_info.updated });
		function.code.push_back(bridge_call(function, ir::NONE, p_names.updated));
	}
	if (p_info.result != NONE) {
		function.result = function.registers[p_info.result]; // What is handed back is held as that kind.
	}
	function.code.push_back(p_info.result == NONE ? Instruction{ Op::ReturnVoid, 0, 0, 0, 0 } : Instruction{ Op::Return, p_info.result, 0, 0, 0 });
	const uint32_t error = add_register(function, Kind::Any);
	function.locals.push_back(ir::Local{ std::string(LOCAL_ERROR), error });
	ir::Handler handler;
	handler.start = 0;
	handler.end = 1;
	handler.target = static_cast<uint32_t>(function.code.size());
	handler.reg = error;
	function.handlers.push_back(handler);
	function.code.push_back(bridge_call(function, ir::NONE, p_names.caught));
	function.code.push_back(Instruction{ Op::ReturnVoid, 0, 0, 0, 0 });
	return function;
}

ir::Class stub_class(const ClassInfo &p_info) {
	ir::Class klass;
	klass.name = p_info.name;
	klass.base = p_info.base;
	klass.element = static_cast<Kind>(p_info.element);
	klass.element_check = p_info.element_check;
	klass.array = p_info.array;
	klass.table = p_info.table;
	klass.dictionary = p_info.dictionary;
	klass.built_in = p_info.built_in;
	klass.is_interface = p_info.is_interface;
	for (uint32_t i = 0; i < p_info.field_count; i++) {
		const FieldInfo &from = p_info.fields[i];
		ir::Field field;
		field.name = from.name;
		field.kind = static_cast<Kind>(from.kind);
		field.klass = from.klass;
		field.starts_null = from.starts_null;
		field.is_public = from.is_public;
		field.is_protected = from.is_protected;
		field.constant = from.constant;
		klass.fields.push_back(std::move(field));
	}
	klass.methods.assign(p_info.methods, p_info.methods + p_info.method_count);
	for (uint32_t i = 0; i < p_info.named_method_count; i++) {
		klass.named_methods.emplace_back(p_info.named_methods[i].name, p_info.named_methods[i].slot);
	}
	klass.constructor = p_info.constructor;
	klass.static_init = p_info.static_init;
	klass.to_string = p_info.to_string;
	klass.interfaces.assign(p_info.interfaces, p_info.interfaces + p_info.interface_count);
	klass.interface_methods.assign(p_info.interface_methods, p_info.interface_methods + p_info.interface_method_count);
	return klass;
}

// The host's own thunks, in Builtin's order.
void add_builtins(const Program &p_program, const Names &p_names, ir::Module &r_module) {
	static const uint8_t object_in[] = { static_cast<uint8_t>(Kind::Obj) };
	static const uint8_t object_out[] = { static_cast<uint8_t>(Kind::Obj) };
	static const uint8_t text_of[] = { static_cast<uint8_t>(Kind::Obj), static_cast<uint8_t>(Kind::Str) };
	static const uint8_t is_type[] = { static_cast<uint8_t>(Kind::Any), static_cast<uint8_t>(Kind::Bool) };
	const auto add = [&](const ThunkInfo &p_info) { r_module.functions.push_back(thunk_function(p_info, p_names)); };
	const uint8_t null_check = static_cast<uint8_t>(Op::NullCheck);
	add(ThunkInfo{ null_check, 0, 0, 0, object_in, 1, 1, nullptr, 0, NONE, NONE, NONE });
	// A function that calls itself until the interpreter's own limit raises Error #1023: the error and its
	// words are the interpreter's, said once.
	const uint32_t self = static_cast<uint32_t>(r_module.functions.size());
	add(ThunkInfo{ static_cast<uint8_t>(Op::Call), ir::NONE, self, 0, nullptr, 0, 0, nullptr, 0, NONE, NONE, NONE });
	add(ThunkInfo{ static_cast<uint8_t>(Op::ObjectText), 1, 0, 0, text_of, 2, 1, nullptr, 0, 1, NONE, NONE });
	const uint32_t entry = p_program.known.entry;
	add(ThunkInfo{ static_cast<uint8_t>(entry == NONE ? Op::ConstNull : Op::New), 0, entry, 0, object_out, 1, 0, nullptr, 0, 0, NONE, NONE });
	for (uint32_t code : { ir::ANY_IS_INT, ir::ANY_IS_UINT, ir::ANY_IS_NUMBER, ir::ANY_IS_STRING, ir::ANY_IS_BOOLEAN }) {
		add(ThunkInfo{ static_cast<uint8_t>(Op::AnyIs), 1, 0, code, is_type, 2, 1, nullptr, 0, 1, NONE, NONE });
	}
	static const uint8_t number_boxed[] = { static_cast<uint8_t>(Kind::F64), static_cast<uint8_t>(Kind::Any) };
	static const uint8_t wide_boxed[] = { static_cast<uint8_t>(Kind::I64), static_cast<uint8_t>(Kind::Any) };
	add(ThunkInfo{ static_cast<uint8_t>(Op::ToAny), 1, 0, 0, number_boxed, 2, 1, nullptr, 0, 1, NONE, NONE });
	add(ThunkInfo{ static_cast<uint8_t>(Op::ToAny), 1, 0, 0, wide_boxed, 2, 1, nullptr, 0, 1, NONE, NONE });
	// An Array of a length (VecNew: the length, whether it is fixed). Not ArrayNew, which takes one number
	// for a length where an element was meant.
	static const uint8_t array_made[] = { static_cast<uint8_t>(Kind::U32), static_cast<uint8_t>(Kind::Bool), static_cast<uint8_t>(Kind::Obj) };
	static const uint32_t array_given[] = { 0, 1 };
	const uint32_t array = p_program.known.array;
	add(ThunkInfo{ static_cast<uint8_t>(array == NONE ? Op::ConstNull : Op::VecNew), 2, array, 0, array_made, 3, 2, array_given, 2, 2, NONE, NONE });
}

} // namespace

ir::Module stub_module(const Program &p_program) {
	ir::Module module;
	module.files.assign(p_program.files, p_program.files + p_program.file_count);
	module.lines.resize(module.files.size());
	for (uint32_t i = 0; i < p_program.class_count; i++) {
		module.classes.push_back(stub_class(p_program.classes[i]));
	}
	for (uint32_t i = 0; i < p_program.global_count; i++) {
		const GlobalInfo &from = p_program.globals[i];
		module.globals.push_back(ir::Global{ from.name, static_cast<Kind>(from.kind), from.klass, from.starts_null });
	}
	for (uint32_t i = 0; i < p_program.string_count; i++) {
		module.strings.emplace_back(p_program.strings[i].data, p_program.strings[i].size);
	}
	Names names;
	const auto name = [&](std::string_view p_method) {
		ir::EngineName entry;
		entry.key = std::string(BRIDGE) + "." + std::string(p_method);
		entry.value = std::string(p_method);
		entry.name = entry.value;
		entry.owner = std::string(BRIDGE);
		module.engine_names.push_back(std::move(entry));
		return static_cast<uint32_t>(module.engine_names.size() - 1);
	};
	names.call = name(BRIDGE_CALL);
	names.caught = name(BRIDGE_CAUGHT);
	names.updated = name(BRIDGE_UPDATED);
	module.interface_methods.assign(p_program.interface_methods, p_program.interface_methods + p_program.interface_method_count);
	for (uint32_t i = 0; i < p_program.function_count; i++) {
		module.functions.push_back(stub(p_program.functions[i], names));
	}
	for (uint32_t i = 0; i < p_program.thunk_count; i++) {
		module.functions.push_back(thunk_function(p_program.thunks[i], names));
	}
	add_builtins(p_program, names, module);
	const KnownClasses &known = p_program.known;
	module.entry_class = known.entry;
	module.error_class = known.error;
	module.type_error_class = known.type_error;
	module.range_error_class = known.range_error;
	module.argument_error_class = known.argument_error;
	module.function_class = known.function;
	module.array_class = known.array;
	module.object_class = known.object;
	module.dictionary_class = known.dictionary;
	module.reference_error_class = known.reference_error;
	module.regexp_class = known.regexp;
	module.date_class = known.date;
	module.syntax_error_class = known.syntax_error;
	module.uri_error_class = known.uri_error;
	module.class_class = known.class_value;
	module.library_function = p_program.library_function;
	return module;
}

} // namespace rt
} // namespace kas3
