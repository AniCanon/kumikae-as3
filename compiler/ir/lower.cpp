#include "lower.h"

#include "ast_walk.h"
#include "builtins.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace kas3 {
namespace ir {

namespace {

struct Loop {
	std::vector<uint32_t> breaks; // Jumps to patch to the loop's end.
	std::vector<uint32_t> continues; // Jumps to patch to where the loop continues.
};

class Lowerer {
	Compilation &compilation_;
	Module &module_;
	bool failed_ = false;

	Symbol *int_ = nullptr;
	Symbol *uint_ = nullptr;
	Symbol *int64_ = nullptr;
	Symbol *number_ = nullptr;
	Symbol *boolean_ = nullptr;
	Symbol *string_ = nullptr;
	Symbol *object_ = nullptr;
	Symbol *error_ = nullptr;
	Symbol *godot_object_ = nullptr; // godot.GodotObject: what every Godot object class extends.
	Symbol *signal_ = nullptr; // godot.Signal
	Symbol *function_ = nullptr; // Function

	std::unordered_map<const Symbol *, uint32_t> classes_;
	std::unordered_map<const Symbol *, uint32_t> functions_; // Methods, constructors, package functions.
	std::unordered_map<const Symbol *, uint32_t> fields_; // Instance fields: the slot.
	std::unordered_map<const Symbol *, uint32_t> globals_; // Static fields and package variables.
	std::unordered_map<const Symbol *, uint32_t> slots_; // Instance methods: the method-table slot.
	std::unordered_map<std::string, uint32_t> strings_;
	std::unordered_map<uint32_t, std::unordered_map<std::string, uint32_t>> slot_names_; // Per class.
	std::unordered_map<const Unit *, uint32_t> files_;

	// The function being lowered.
	Unit *unit_ = nullptr;
	uint32_t function_index_ = 0; // By index: the list of functions can grow while a body is lowered.
	Symbol *class_ = nullptr;
	bool constructor_ = false;
	std::unordered_map<const Symbol *, uint32_t> locals_;
	std::vector<Loop> loops_;

	Function &fn() { return module_.functions[function_index_]; }
	const Function &fn() const { return module_.functions[function_index_]; }

	// ---- reporting ----

	void refuse(uint32_t p_offset, const std::string &p_what) {
		failed_ = true;
		if (unit_) {
			unit_->diagnostics.error(p_offset, p_what + " is not supported by the interpreter yet");
		}
	}

	// ---- tables ----

	uint32_t string(const std::string &p_text) {
		auto found = strings_.find(p_text);
		if (found != strings_.end()) {
			return found->second;
		}
		module_.strings.push_back(p_text);
		return strings_[p_text] = (uint32_t)module_.strings.size() - 1;
	}

	uint32_t wide(int64_t p_value) {
		for (size_t i = 0; i < module_.wides.size(); i++) {
			if (module_.wides[i] == p_value) {
				return (uint32_t)i;
			}
		}
		module_.wides.push_back(p_value);
		return (uint32_t)module_.wides.size() - 1;
	}

	// A whole number written out (`12`, `0xFF`, `-5`) that an Int64 holds: its exact value. The checker
	// has refused one that is written where an Int64 is expected and does not fit.
	static bool wide_literal(const Expr *p_expression, int64_t *r_value) {
		bool negative = false;
		while (p_expression && p_expression->kind == NodeKind::Unary &&
				(static_cast<const Unary *>(p_expression)->op == TokenKind::Minus || static_cast<const Unary *>(p_expression)->op == TokenKind::Plus)) {
			negative = negative != (static_cast<const Unary *>(p_expression)->op == TokenKind::Minus);
			p_expression = static_cast<const Unary *>(p_expression)->operand;
		}
		if (!p_expression || p_expression->kind != NodeKind::NumberLit) {
			return false;
		}
		std::string_view text = static_cast<const NumberLit *>(p_expression)->text;
		bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
		if (!hex && text.find_first_of(".eE") != std::string_view::npos) {
			return false;
		}
		uint64_t value = 0;
		const uint64_t base = hex ? 16 : 10;
		for (size_t i = hex ? 2 : 0; i < text.size(); i++) {
			char c = text[i];
			uint64_t digit = c >= '0' && c <= '9' ? (uint64_t)(c - '0') : c >= 'a' && c <= 'f' ? (uint64_t)(c - 'a' + 10) : c >= 'A' && c <= 'F' ? (uint64_t)(c - 'A' + 10) : 99;
			if (digit >= base || value > (UINT64_MAX - digit) / base) {
				return false;
			}
			value = value * base + digit;
		}
		if (value > (negative ? (uint64_t)1 << 63 : (uint64_t)INT64_MAX)) {
			return false;
		}
		*r_value = negative ? (int64_t)(0 - value) : (int64_t)value;
		return true;
	}

	uint32_t number(double p_value) {
		for (size_t i = 0; i < module_.numbers.size(); i++) {
			// Bit-for-bit: 0 and -0 are different constants, and NaN is itself.
			if (std::memcmp(&module_.numbers[i], &p_value, sizeof(double)) == 0) {
				return (uint32_t)i;
			}
		}
		module_.numbers.push_back(p_value);
		return (uint32_t)module_.numbers.size() - 1;
	}

	uint32_t file(const Unit *p_unit) {
		auto found = files_.find(p_unit);
		if (found != files_.end()) {
			return found->second;
		}
		module_.files.push_back(p_unit->source.path());
		std::vector<uint32_t> lines;
		for (int line = 1; line <= p_unit->source.line_count(); line++) {
			lines.push_back(p_unit->source.line_start(line));
		}
		module_.lines.push_back(std::move(lines));
		return files_[p_unit] = (uint32_t)module_.files.size() - 1;
	}

	std::string class_name(const Symbol *p_class) const {
		return p_class->package && !p_class->package->name.empty() ? p_class->package->name + "." + std::string(p_class->name)
																   : std::string(p_class->name);
	}

	// ---- types ----

	bool kind_of(const Type *p_type, Kind *r_kind) const {
		if (!p_type) {
			return false;
		}
		if (p_type->kind == Type::Void) {
			*r_kind = Kind::Void;
			return true;
		}
		if (p_type->kind != Type::Instance || !p_type->symbol) {
			return false;
		}
		const Symbol *symbol = p_type->symbol;
		if (symbol == int_) {
			*r_kind = Kind::I32;
		} else if (symbol == uint_) {
			*r_kind = Kind::U32;
		} else if (symbol == int64_) {
			*r_kind = Kind::I64;
		} else if (symbol == number_) {
			*r_kind = Kind::F64;
		} else if (symbol == boolean_) {
			*r_kind = Kind::Bool;
		} else if (symbol == string_) {
			*r_kind = Kind::Str;
		} else if (classes_.count(symbol) || is_error(symbol)) {
			*r_kind = Kind::Obj;
		} else if (is_godot(symbol) && symbol->kind == SymbolKind::Class) {
			*r_kind = is_godot_object(symbol) ? Kind::Ref : Kind::Val;
		} else {
			return false;
		}
		return true;
	}

	// Declared in Godot's API.
	static bool is_godot(const Symbol *p_symbol) {
		return p_symbol && p_symbol->unit && p_symbol->unit->is_declaration && p_symbol->unit->source.path() == GODOT_DECLARATIONS_PATH;
	}

	// A Godot class whose objects the engine owns (a node, a resource), as against a value type.
	bool is_godot_object(const Symbol *p_class) const {
		for (const Symbol *at = p_class; at && at->klass; at = at->klass->base) {
			if (at == godot_object_) {
				return true;
			}
		}
		return false;
	}

	// Godot's own name for a member or (with no member) a class it declares.
	bool engine_name(const Symbol *p_class, std::string_view p_member, uint32_t *r_string) {
		std::string name;
		if (!p_class || !godot_name(p_class->name, p_member, &name)) {
			return false;
		}
		*r_string = string(name);
		return true;
	}

	// The kind a written type names; refused if the interpreter has no such values yet.
	Kind written_kind(TypeRef *p_type, uint32_t p_offset, Kind p_missing) {
		if (!p_type) {
			if (p_missing == Kind::Void) {
				return Kind::Void;
			}
			refuse(p_offset, "a declaration without a type");
			return Kind::F64;
		}
		Kind kind = Kind::Void;
		if (!kind_of(compilation_.type_of(p_type), &kind)) {
			refuse(p_type->offset, "the type '" + compilation_.type_name(compilation_.type_of(p_type)) + "'");
			return Kind::F64;
		}
		return kind;
	}

	// A built-in class that is Error or extends it. These are the only built-in classes with objects so far.
	bool is_error(const Symbol *p_class) const {
		for (const Symbol *at = p_class; at && at->klass; at = at->klass->base) {
			if (at == error_) {
				return at->unit && at->unit->is_builtin;
			}
		}
		return false;
	}

	// ---- declarations ----

	// The class's index, creating the built-in Error classes on first use.
	bool class_index(const Symbol *p_class, uint32_t *r_index) {
		auto found = classes_.find(p_class);
		if (found != classes_.end()) {
			*r_index = found->second;
			return true;
		}
		if (!p_class || !p_class->unit || !p_class->unit->is_builtin || !is_error(p_class)) {
			return false;
		}
		// Error and its built-in subclasses: one field, and a constructor that stores the message.
		uint32_t base = NONE;
		if (p_class != error_ && !class_index(p_class->klass->base, &base)) {
			return false;
		}
		Class klass;
		klass.name = class_name(p_class);
		klass.base = base;
		klass.fields.push_back({ "message", Kind::Str });
		Function constructor;
		constructor.name = klass.name;
		constructor.owner = (uint32_t)module_.classes.size();
		constructor.parameters = 2;
		constructor.registers = { Kind::Obj, Kind::Str };
		Instruction store;
		store.op = Op::SetField;
		store.a = 0;
		store.b = 0;
		store.c = 1;
		constructor.code.push_back(store);
		Instruction done;
		done.op = Op::ReturnVoid;
		constructor.code.push_back(done);
		klass.constructor = (uint32_t)module_.functions.size();
		module_.functions.push_back(std::move(constructor));
		module_.classes.push_back(std::move(klass));
		uint32_t index = (uint32_t)module_.classes.size() - 1;
		classes_[p_class] = index;
		if (p_class->klass->constructor) {
			functions_[p_class->klass->constructor] = module_.classes[index].constructor;
		}
		Symbol *message = nullptr;
		auto member = error_->klass->members.find("message");
		if (member != error_->klass->members.end()) {
			message = member->second;
			fields_[message] = 0;
		}
		*r_index = index;
		return true;
	}

	// Gives the class its place in the module, before any class is filled in: a field may be of a class
	// declared later.
	void reserve_class(ClassDecl *p_decl) {
		Symbol *symbol = p_decl->symbol;
		if (!symbol || !symbol->klass || classes_.count(symbol)) {
			return;
		}
		Class klass;
		klass.name = class_name(symbol);
		classes_[symbol] = (uint32_t)module_.classes.size();
		module_.classes.push_back(std::move(klass));
	}

	void declare_class(Unit &p_unit, ClassDecl *p_decl) {
		Symbol *symbol = p_decl->symbol;
		if (!symbol || !symbol->klass) {
			return;
		}
		unit_ = &p_unit;
		if (p_decl->implements.count > 0) {
			refuse(p_decl->implements[0]->offset, "'implements'");
		}
		Class klass;
		klass.name = class_name(symbol);
		Symbol *base = symbol->klass->base;
		std::string godot_base;
		if (base && is_godot(base) && is_godot_object(base) && godot_name(base->name, std::string_view(), &godot_base)) {
			// A script: it is attached to an object of that Godot class, kept in its first field.
			klass.native_base = godot_base;
			klass.fields.push_back({ "$owner", Kind::Ref });
		} else if (base && base != object_) {
			if (!class_index(base, &klass.base)) {
				refuse(p_decl->extends ? p_decl->extends->offset : p_decl->name_offset,
						"extending '" + std::string(base->name) + "'");
			} else {
				klass.fields = module_.classes[klass.base].fields;
				klass.methods = module_.classes[klass.base].methods;
				klass.to_string = module_.classes[klass.base].to_string;
				klass.native_base = module_.classes[klass.base].native_base;
				klass.virtuals = module_.classes[klass.base].virtuals;
			}
		}
		uint32_t index = classes_[symbol];
		if (klass.base != NONE) {
			slot_names_[index] = slot_names_[klass.base];
		}
		module_.classes[index] = std::move(klass);

		auto new_function = [&](FunctionDecl *p_function, const std::string &p_name, bool p_static) {
			Function function;
			function.name = p_name;
			function.file = file(&p_unit);
			function.position = p_function ? p_function->name_offset : p_decl->name_offset;
			function.owner = index;
			function.is_static = p_static;
			module_.functions.push_back(std::move(function));
			return (uint32_t)module_.functions.size() - 1;
		};
		const std::string name = module_.classes[index].name;
		bool has_constructor = false;
		for (Node *member : p_decl->members) {
			if (member->kind == NodeKind::VarDecl) {
				VarDecl *decl = static_cast<VarDecl *>(member);
				for (Declarator &declarator : decl->declarators) {
					if (!declarator.symbol) {
						continue;
					}
					Kind kind = written_kind(declarator.type, declarator.offset, Kind::F64);
					if (decl->modifiers.bits & Modifiers::Static) {
						globals_[declarator.symbol] = (uint32_t)module_.globals.size();
						module_.globals.push_back({ name + "." + std::string(declarator.name), kind });
					} else {
						fields_[declarator.symbol] = (uint32_t)module_.classes[index].fields.size();
						module_.classes[index].fields.push_back({ std::string(declarator.name), kind });
					}
				}
			} else if (member->kind == NodeKind::FunctionDecl) {
				FunctionDecl *function = static_cast<FunctionDecl *>(member);
				if (!function->symbol) {
					continue;
				}
				if (function->accessor != FunctionDecl::None) {
					refuse(function->name_offset, "a getter or setter");
					continue;
				}
				bool is_static = (function->modifiers.bits & Modifiers::Static) != 0;
				if (symbol->klass->constructor == function->symbol) {
					has_constructor = true;
					uint32_t made = new_function(function, name, false);
					module_.classes[index].constructor = made;
					functions_[function->symbol] = made;
					continue;
				}
				uint32_t made = new_function(function, name + "/" + std::string(function->name), is_static);
				functions_[function->symbol] = made;
				if (function->name == "toString" && !is_static) {
					const Type *result = function->return_type ? compilation_.type_of(function->return_type) : nullptr;
					if (function->parameters.count == 0 && result && result->kind == Type::Instance && result->symbol == string_) {
						module_.classes[index].to_string = made;
					} else {
						refuse(function->name_offset, "a toString() that takes arguments or does not return a String");
					}
				}
				if (is_static) {
					continue;
				}
				// A method that overrides one of Godot's is what the engine calls under Godot's name.
				if (function->modifiers.bits & Modifiers::Override) {
					for (Symbol *at = symbol->klass->base; at && at->klass; at = at->klass->base) {
						auto inherited = at->klass->members.find(function->name);
						if (inherited == at->klass->members.end()) {
							continue;
						}
						std::string godot;
						if (!is_godot(inherited->second)) {
							continue; // A script class's override of it: Godot's declaration is further up.
						}
						if (godot_name(at->name, function->name, &godot)) {
							bool replaced = false;
							for (auto &entry : module_.classes[index].virtuals) {
								if (entry.first == godot) {
									entry.second = made;
									replaced = true;
								}
							}
							if (!replaced) {
								module_.classes[index].virtuals.push_back({ godot, made });
							}
						}
						break;
					}
				}
				// An override takes the slot of what it overrides.
				std::unordered_map<std::string, uint32_t> &names = slot_names_[index];
				std::string key(function->name);
				auto slot = names.find(key);
				if (slot != names.end() && (function->modifiers.bits & Modifiers::Override)) {
					module_.classes[index].methods[slot->second] = made;
					slots_[function->symbol] = slot->second;
				} else {
					uint32_t added = (uint32_t)module_.classes[index].methods.size();
					module_.classes[index].methods.push_back(made);
					// A private method is not seen by subclasses, so its name is not recorded for them.
					if (!(function->modifiers.bits & Modifiers::Private)) {
						names[key] = added;
					}
					slots_[function->symbol] = added;
				}
			}
		}
		if (!has_constructor) {
			module_.classes[index].constructor = new_function(nullptr, name, false);
		}
	}

	// ---- emitting ----

	uint32_t emit(Op p_op, uint32_t p_position, uint32_t p_a = 0, uint32_t p_b = 0, uint32_t p_c = 0) {
		Instruction instruction;
		instruction.op = p_op;
		instruction.a = p_a;
		instruction.b = p_b;
		instruction.c = p_c;
		instruction.position = p_position;
		fn().code.push_back(instruction);
		return (uint32_t)fn().code.size() - 1;
	}

	uint32_t here() const { return (uint32_t)fn().code.size(); }

	void patch(uint32_t p_jump, uint32_t p_target) {
		Instruction &instruction = fn().code[p_jump];
		(instruction.op == Op::Jump ? instruction.a : instruction.b) = p_target;
	}

	uint32_t reg(Kind p_kind) {
		fn().registers.push_back(p_kind);
		return (uint32_t)fn().registers.size() - 1;
	}

	// A register for what the engine gives back. The host checks what arrives against the type.
	uint32_t engine_reg(Kind p_kind, const Type *p_type) {
		uint32_t out = reg(p_kind);
		std::string godot;
		if (is_engine(p_kind) && p_type && p_type->symbol && godot_name(p_type->symbol->name, std::string_view(), &godot)) {
			if (fn().engine_types.size() <= out) {
				fn().engine_types.resize(out + 1);
			}
			fn().engine_types[out] = godot;
		}
		return out;
	}

	Kind kind_at(uint32_t p_reg) const {
		return p_reg < fn().registers.size() ? fn().registers[p_reg] : Kind::Void;
	}

	uint32_t arguments(std::vector<uint32_t> p_list) {
		fn().argument_lists.push_back(std::move(p_list));
		return (uint32_t)fn().argument_lists.size() - 1;
	}

	// The default value of a kind, in a new register.
	uint32_t default_value(Kind p_kind, uint32_t p_position) {
		uint32_t out = reg(p_kind == Kind::Void ? Kind::F64 : p_kind);
		switch (p_kind) {
			case Kind::I32:
				emit(Op::ConstI32, p_position, out, 0);
				break;
			case Kind::U32:
				emit(Op::ConstU32, p_position, out, 0);
				break;
			case Kind::Bool:
				emit(Op::ConstBool, p_position, out, 0);
				break;
			case Kind::I64:
				emit(Op::ConstI64, p_position, out, wide(0));
				break;
			case Kind::Str:
			case Kind::Obj:
			case Kind::Val:
			case Kind::Ref:
				emit(Op::ConstNull, p_position, out);
				break;
			default:
				emit(Op::ConstF64, p_position, out, number(std::numeric_limits<double>::quiet_NaN()));
				break;
		}
		return out;
	}

	// The value in p_reg as p_to, in a new register unless it is that kind already.
	// p_written: the program wrote the conversion itself (`int(x)`). That matters for an Int64 only: a
	// conversion that is not written raises a RangeError when the value does not fit, and one that is
	// written is AS3's own, which cuts, rounds or holds at the end.
	uint32_t convert(uint32_t p_reg, Kind p_to, uint32_t p_position, bool p_written = false) {
		Kind from = kind_at(p_reg);
		if (from == p_to || p_to == Kind::Void) {
			return p_reg;
		}
		if (from == Kind::I64 || p_to == Kind::I64) {
			auto step = [&](Op p_op, Kind p_kind, uint32_t p_source) {
				uint32_t out = reg(p_kind);
				emit(p_op, p_position, out, p_source);
				return out;
			};
			if (from == Kind::I64) {
				switch (p_to) {
					case Kind::I32:
						return step(p_written ? Op::I64WrapI32 : Op::I64ToI32, Kind::I32, p_reg);
					case Kind::U32:
						return step(p_written ? Op::I64WrapU32 : Op::I64ToU32, Kind::U32, p_reg);
					case Kind::F64:
						return step(p_written ? Op::I64RoundF64 : Op::I64ToF64, Kind::F64, p_reg);
					case Kind::Bool:
						return step(Op::I64ToBool, Kind::Bool, p_reg);
					case Kind::Str:
						return step(Op::I64ToStr, Kind::Str, p_reg);
					default:
						refuse(p_position, is_engine(p_to) ? "passing this value where a Godot value is expected" : "converting a value to an object");
						return default_value(p_to, p_position);
				}
			}
			switch (from) {
				case Kind::I32:
					return step(Op::I32ToI64, Kind::I64, p_reg);
				case Kind::U32:
					return step(Op::U32ToI64, Kind::I64, p_reg);
				case Kind::F64:
					return step(p_written ? Op::F64ClampI64 : Op::F64ToI64, Kind::I64, p_reg);
				case Kind::Bool:
					return step(Op::F64ClampI64, Kind::I64, step(Op::BoolToF64, Kind::F64, p_reg));
				case Kind::Str:
					return step(Op::StrToI64, Kind::I64, p_reg);
				default:
					refuse(p_position, is_engine(from) ? "converting a Godot value" : "converting an object to a number");
					return default_value(Kind::I64, p_position);
			}
		}
		if (is_engine(from) && p_to == Kind::Str) {
			uint32_t out = reg(Kind::Str);
			emit(Op::HostToStr, p_position, out, p_reg);
			return out;
		}
		if (is_engine(from) || is_engine(p_to)) {
			refuse(p_position, is_engine(from) ? "converting a Godot value" : "passing this value where a Godot value is expected");
			return default_value(p_to, p_position);
		}
		auto via = [&](Op p_op, Kind p_kind, uint32_t p_source) {
			uint32_t out = reg(p_kind);
			emit(p_op, p_position, out, p_source);
			return out;
		};
		auto to_number = [&]() -> uint32_t {
			switch (from) {
				case Kind::I32:
					return via(Op::I32ToF64, Kind::F64, p_reg);
				case Kind::U32:
					return via(Op::U32ToF64, Kind::F64, p_reg);
				case Kind::Bool:
					return via(Op::BoolToF64, Kind::F64, p_reg);
				case Kind::Str:
					return via(Op::StrToF64, Kind::F64, p_reg);
				case Kind::F64:
					return p_reg;
				default:
					refuse(p_position, "converting an object to a number");
					return via(Op::ConstF64, Kind::F64, number(0));
			}
		};
		switch (p_to) {
			case Kind::F64:
				return to_number();
			case Kind::I32:
				if (from == Kind::U32) {
					return via(Op::U32ToI32, Kind::I32, p_reg);
				}
				return via(Op::F64ToI32, Kind::I32, to_number());
			case Kind::U32:
				if (from == Kind::I32) {
					return via(Op::I32ToU32, Kind::U32, p_reg);
				}
				return via(Op::F64ToU32, Kind::U32, to_number());
			case Kind::Bool:
				switch (from) {
					case Kind::I32:
						return via(Op::I32ToBool, Kind::Bool, p_reg);
					case Kind::U32:
						return via(Op::U32ToBool, Kind::Bool, p_reg);
					case Kind::F64:
						return via(Op::F64ToBool, Kind::Bool, p_reg);
					case Kind::Str:
						return via(Op::StrToBool, Kind::Bool, p_reg);
					default:
						return via(Op::ObjToBool, Kind::Bool, p_reg);
				}
			case Kind::Str:
				switch (from) {
					case Kind::I32:
						return via(Op::I32ToStr, Kind::Str, p_reg);
					case Kind::U32:
						return via(Op::U32ToStr, Kind::Str, p_reg);
					case Kind::F64:
						return via(Op::F64ToStr, Kind::Str, p_reg);
					case Kind::Bool:
						return via(Op::BoolToStr, Kind::Str, p_reg);
					default:
						return via(Op::ObjToStr, Kind::Str, p_reg);
				}
			case Kind::Obj:
				refuse(p_position, "converting a value to an object");
				return default_value(Kind::Obj, p_position);
			default:
				return p_reg;
		}
	}

	// ---- expressions ----

	Kind natural(Expr *p_expression) {
		Kind kind = Kind::Void;
		if (p_expression->type && p_expression->type->kind == Type::Null) {
			return Kind::Obj;
		}
		if (!kind_of(p_expression->type, &kind)) {
			refuse(p_expression->offset, "a value of type '" + compilation_.type_name(p_expression->type) + "'");
			return Kind::F64;
		}
		return kind;
	}

	static bool is_null(const Expr *p_expression) {
		return p_expression->kind == NodeKind::NullLit;
	}

	// True if evaluating the expression can change a local variable.
	static bool writes_locals(Expr *p_expression) {
		bool writes = false;
		walk(p_expression, [&](Node *p_node) {
			if (p_node->kind == NodeKind::Assign || p_node->kind == NodeKind::Postfix) {
				writes = true;
			} else if (p_node->kind == NodeKind::Unary) {
				TokenKind op = static_cast<Unary *>(p_node)->op;
				writes = writes || op == TokenKind::PlusPlus || op == TokenKind::MinusMinus;
			}
		});
		return writes;
	}

	// A value that a later operand must not be able to change: a local's own register is copied if p_later
	// can write locals.
	uint32_t settled(uint32_t p_reg, Expr *p_later, uint32_t p_position) {
		if (!p_later || !writes_locals(p_later)) {
			return p_reg;
		}
		for (const auto &local : locals_) {
			if (local.second == p_reg) {
				uint32_t copy = reg(kind_at(p_reg));
				emit(Op::Move, p_position, copy, p_reg);
				return copy;
			}
		}
		return p_reg;
	}

	// Evaluates the expression and gives the register holding its value as p_want (Void: as it comes).
	uint32_t expression(Expr *p_expression, Kind p_want) {
		if (!p_expression) {
			return default_value(p_want, 0);
		}
		if (is_null(p_expression)) {
			// null is whatever is asked for: no reference, 0, or false.
			uint32_t out = default_value(p_want == Kind::Void ? Kind::Obj : p_want == Kind::F64 ? Kind::I32 : p_want, p_expression->offset);
			return p_want == Kind::F64 ? convert(out, Kind::F64, p_expression->offset) : out;
		}
		uint32_t value = evaluate(p_expression, p_want);
		return convert(value, p_want, p_expression->offset);
	}

	static double literal_value(const NumberLit *p_literal) {
		std::string text(p_literal->text);
		if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
			double value = 0;
			for (size_t i = 2; i < text.size(); i++) {
				char c = text[i];
				int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0;
				value = value * 16 + digit;
			}
			return value;
		}
		return std::strtod(text.c_str(), nullptr);
	}

	// The characters a string literal stands for.
	static std::string unescape(std::string_view p_text) {
		std::string out;
		if (p_text.size() < 2) {
			return out;
		}
		auto hex = [&](size_t p_at, size_t p_count, uint32_t *r_value) {
			uint32_t value = 0;
			if (p_at + p_count > p_text.size() - 1) {
				return false;
			}
			for (size_t i = 0; i < p_count; i++) {
				char c = p_text[p_at + i];
				int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
				if (digit < 0) {
					return false;
				}
				value = value * 16 + (uint32_t)digit;
			}
			*r_value = value;
			return true;
		};
		auto utf8 = [&](uint32_t p_code) {
			if (p_code < 0x80) {
				out += (char)p_code;
			} else if (p_code < 0x800) {
				out += (char)(0xC0 | (p_code >> 6));
				out += (char)(0x80 | (p_code & 0x3F));
			} else {
				out += (char)(0xE0 | (p_code >> 12));
				out += (char)(0x80 | ((p_code >> 6) & 0x3F));
				out += (char)(0x80 | (p_code & 0x3F));
			}
		};
		for (size_t i = 1; i + 1 < p_text.size(); i++) {
			char c = p_text[i];
			if (c != '\\' || i + 2 >= p_text.size()) {
				out += c;
				continue;
			}
			char escape = p_text[++i];
			uint32_t code = 0;
			switch (escape) {
				case 'n':
					out += '\n';
					break;
				case 't':
					out += '\t';
					break;
				case 'r':
					out += '\r';
					break;
				case 'b':
					out += '\b';
					break;
				case 'f':
					out += '\f';
					break;
				case '0':
					out += '\0';
					break;
				case 'x':
					if (hex(i + 1, 2, &code)) {
						utf8(code);
						i += 2;
					} else {
						out += 'x';
					}
					break;
				case 'u':
					if (hex(i + 1, 4, &code)) {
						utf8(code);
						i += 4;
					} else {
						out += 'u';
					}
					break;
				default:
					out += escape; // Quotes, the backslash, and anything else stand for themselves.
					break;
			}
		}
		return out;
	}

	// NaN, Infinity, and the limits of the number types: constants of the built-in library.
	bool builtin_constant(const Symbol *p_symbol, uint32_t p_position, uint32_t *r_reg) {
		if (!p_symbol || !p_symbol->unit || !p_symbol->unit->is_builtin ||
				(p_symbol->kind != SymbolKind::Variable && p_symbol->kind != SymbolKind::Field)) {
			return false;
		}
		const Symbol *owner = p_symbol->owner;
		std::string_view name = p_symbol->name;
		const double infinity = std::numeric_limits<double>::infinity();
		const double nan = std::numeric_limits<double>::quiet_NaN();
		if (!owner || owner == number_) {
			double value = 0;
			if (name == "NaN") {
				value = nan;
			} else if ((!owner && name == "Infinity") || (owner && name == "POSITIVE_INFINITY")) {
				value = infinity;
			} else if (owner && name == "NEGATIVE_INFINITY") {
				value = -infinity;
			} else if (owner && name == "MAX_VALUE") {
				value = std::numeric_limits<double>::max();
			} else if (owner && name == "MIN_VALUE") {
				value = std::numeric_limits<double>::denorm_min(); // The smallest above zero, as in AS3.
			} else {
				return false;
			}
			*r_reg = reg(Kind::F64);
			emit(Op::ConstF64, p_position, *r_reg, number(value));
			return true;
		}
		if (owner == int_ && (name == "MAX_VALUE" || name == "MIN_VALUE")) {
			*r_reg = reg(Kind::I32);
			emit(Op::ConstI32, p_position, *r_reg, name == "MAX_VALUE" ? 0x7FFFFFFFu : 0x80000000u);
			return true;
		}
		if (owner == int64_ && (name == "MAX_VALUE" || name == "MIN_VALUE")) {
			*r_reg = reg(Kind::I64);
			emit(Op::ConstI64, p_position, *r_reg, wide(name == "MAX_VALUE" ? INT64_MAX : INT64_MIN));
			return true;
		}
		if (owner == uint_ && (name == "MAX_VALUE" || name == "MIN_VALUE")) {
			*r_reg = reg(Kind::U32);
			emit(Op::ConstU32, p_position, *r_reg, name == "MAX_VALUE" ? 0xFFFFFFFFu : 0u);
			return true;
		}
		return false;
	}

	Symbol *callee_symbol(Expr *p_callee) {
		if (p_callee->kind == NodeKind::Name) {
			return static_cast<Name *>(p_callee)->symbol;
		}
		if (p_callee->kind == NodeKind::Member) {
			Member *member = static_cast<Member *>(p_callee);
			return member->symbol ? member->symbol : member->member;
		}
		return nullptr;
	}

	// Marks a use of a class from outside it: its static fields get their values the first time. Code inside
	// the class needs no mark, since it can only be running because the class was used.
	void touch(const Symbol *p_class, uint32_t p_position) {
		auto found = classes_.find(p_class);
		if (found != classes_.end() && p_class != class_) {
			emit(Op::InitClass, p_position, found->second);
		}
	}

	// The object a member is reached through, checked for null unless it is `this` or `super`.
	uint32_t receiver(Expr *p_object) {
		if (p_object->kind == NodeKind::ThisExpr || p_object->kind == NodeKind::SuperExpr) {
			return 0;
		}
		uint32_t object = expression(p_object, Kind::Void);
		if (kind_at(object) != Kind::Obj) {
			refuse(p_object->offset, "a member of a value of type '" + compilation_.type_name(p_object->type) + "'");
			return default_value(Kind::Obj, p_object->offset);
		}
		// No check here: a missing object is reported where it is used, after whatever else the expression
		// evaluates first (a call's arguments, an assignment's value), as in Flash.
		return object;
	}

	struct Part;
	// A place a value can be stored: a local, a field of an object, or a global.
	struct Place {
		enum Where { Nowhere, Register, FieldOf, GlobalSlot, Engine } where = Nowhere;
		uint32_t index = 0; // The register, the field slot, the global, or the string of Godot's name.
		uint32_t object = 0; // For FieldOf and Engine: the register holding the object.
		Kind kind = Kind::F64;
		bool of_value = false; // For Engine: a member of a value type, which is a copy.
		const Type *type = nullptr; // For Engine: what the member is declared to be.
		uint32_t key = NONE; // For Engine: the place is an element (`value[key]`); the register holding the key.
		bool constant = false; // One of Godot's constants: read from a register, and never written.
		// For Engine: the member is one part of a Godot value that is itself stored somewhere
		// (`position.x`). Godot's values are copies, so writing the part is: read the whole value, change
		// the part, write the whole value back.
		std::shared_ptr<Part> part;
	};
	struct Part {
		Place whole; // Where the value is stored.
		uint32_t value = NONE; // The register it was read into, once it has been.
	};

	// The value a part belongs to. It is read where the place is worked out, before anything to the right
	// of it runs, as GDScript does: `position.x = f()` keeps the `y` that `position` had before `f()`.
	uint32_t whole_of(const Place &p_place, uint32_t p_position) {
		if (p_place.part->value == NONE) {
			p_place.part->value = load(p_place.part->whole, p_position);
		}
		return p_place.part->value;
	}

	// The engine object a Godot member is reached through: the script's own owner when nothing is
	// written before the name, or the value of what is written.
	uint32_t engine_receiver(Expr *p_object, uint32_t p_position, bool *r_of_value) {
		*r_of_value = false;
		if (!p_object || p_object->kind == NodeKind::ThisExpr || p_object->kind == NodeKind::SuperExpr) {
			uint32_t klass = 0;
			if (fn().is_static || !class_ || !class_index(class_, &klass) || module_.classes[klass].native_base.empty()) {
				refuse(p_position, "this use of a Godot member");
				return default_value(Kind::Ref, p_position);
			}
			uint32_t owner = reg(Kind::Ref);
			emit(Op::GetField, p_position, owner, 0, 0);
			return owner;
		}
		uint32_t object = expression(p_object, Kind::Void);
		Kind kind = kind_at(object);
		if (kind == Kind::Ref || kind == Kind::Val) {
			*r_of_value = kind == Kind::Val;
			return object;
		}
		// Another script object: its owner.
		uint32_t klass = 0;
		const Type *type = p_object->type;
		if (kind == Kind::Obj && type && type->kind == Type::Instance && class_index(type->symbol, &klass) &&
				!module_.classes[klass].native_base.empty()) {
			emit(Op::NullCheck, p_object->offset, object);
			uint32_t owner = reg(Kind::Ref);
			emit(Op::GetField, p_position, owner, object, 0);
			return owner;
		}
		refuse(p_position, "this use of a Godot member");
		return default_value(Kind::Ref, p_position);
	}

	// One of Godot's values that is made of parts (a Vector2's x and y, a Color's r, g, b and a) is never
	// missing unless a program says so: a variable of it with no value written starts at the type's zero,
	// as it does in GDScript. This gives that zero in a new register, or NONE for any other type (an array,
	// a signal, a function), which starts missing as an object does.
	uint32_t zero_value(TypeRef *p_written, uint32_t p_position, uint32_t p_into = NONE) {
		const Type *type = p_written ? compilation_.type_of(p_written) : nullptr;
		Kind kind = Kind::Void;
		uint32_t name = 0;
		if (!type || type->kind != Type::Instance || !type->symbol || !type->symbol->klass || !kind_of(type, &kind) || kind != Kind::Val ||
				!engine_name(type->symbol, std::string_view(), &name)) {
			return NONE;
		}
		bool parts = false;
		for (const auto &member : type->symbol->klass->members) {
			parts = parts || (member.second->kind == SymbolKind::Field && !member.second->is_static());
		}
		if (!parts) {
			return NONE;
		}
		uint32_t out = p_into != NONE ? p_into : reg(Kind::Val);
		emit(Op::HostNew, p_position, out, name, arguments({}));
		return out;
	}

	// One of Godot's values that stores 32-bit integers: every part of it is an `int`, or is such a value
	// (Vector2i; Rect2i, which is two of them). Godot cuts a wider integer off inside these without a
	// word, so a whole number on its way into one goes as an `int`, and says so if it does not fit.
	bool stores_int32(const Type *p_type) const {
		if (!p_type || p_type->kind != Type::Instance || !p_type->symbol || !p_type->symbol->klass || !is_godot(p_type->symbol) ||
				is_godot_object(p_type->symbol)) {
			return false;
		}
		bool any = false;
		for (const auto &member : p_type->symbol->klass->members) {
			if (member.second->kind != SymbolKind::Field || member.second->is_static()) {
				continue;
			}
			const Type *part = member.second->type ? compilation_.type_of(member.second->type) : nullptr;
			Kind kind = Kind::Void;
			if (!part || part == p_type || !((kind_of(part, &kind) && kind == Kind::I32) || stores_int32(part))) {
				return false;
			}
			any = true;
		}
		return any;
	}

	// A whole number in p_reg, as the `int` a 32-bit value takes: checked, never cut.
	uint32_t as_int32(uint32_t p_reg, uint32_t p_position) {
		Kind kind = kind_at(p_reg);
		if (kind == Kind::I32 || !is_numeric(kind)) {
			return p_reg;
		}
		return convert(convert(p_reg, Kind::I64, p_position), Kind::I32, p_position);
	}

	// A class Godot keeps one object of (Input): tagged [Singleton] in the declarations.
	static bool is_singleton(const Symbol *p_class) {
		if (!p_class || p_class->kind != SymbolKind::Class || !p_class->unit || !p_class->unit->is_declaration || !p_class->node ||
				p_class->node->kind != NodeKind::ClassDecl) {
			return false;
		}
		for (const Metadata &tag : static_cast<ClassDecl *>(p_class->node)->metadata) {
			if (tag.name == "Singleton") {
				return true;
			}
		}
		return false;
	}

	// The engine's one object of a singleton class. Everything written `Input.x` is then a member of it.
	uint32_t singleton(const Symbol *p_class, uint32_t p_position) {
		uint32_t name = 0;
		if (!engine_name(p_class, std::string_view(), &name)) {
			refuse(p_position, "this use of a Godot member");
			return default_value(Kind::Ref, p_position);
		}
		uint32_t out = reg(Kind::Ref);
		emit(Op::HostSingleton, p_position, out, name);
		return out;
	}

	// One of Godot's constants. One with a value in the declarations (`Key.ESCAPE`) is that value; any
	// other (`Vector2.ZERO`) is asked of the engine.
	Place constant_place(Symbol *p_symbol, uint32_t p_position) {
		Place place;
		Kind kind = Kind::Void;
		const Type *type = p_symbol->type ? compilation_.type_of(p_symbol->type) : nullptr;
		uint32_t klass = 0, name = 0;
		Expr *value = nullptr;
		if (p_symbol->node && p_symbol->node->kind == NodeKind::VarDecl) {
			for (const Declarator &declarator : static_cast<VarDecl *>(p_symbol->node)->declarators) {
				value = declarator.symbol == p_symbol ? declarator.initializer : value;
			}
		}
		if (p_symbol->kind != SymbolKind::Field || !type || !kind_of(type, &kind) || kind == Kind::Void ||
				(!value && (!engine_name(p_symbol->owner, std::string_view(), &klass) || !engine_name(p_symbol->owner, p_symbol->name, &name)))) {
			refuse(p_position, "'" + std::string(p_symbol->name) + "' of Godot's " + std::string(p_symbol->owner ? p_symbol->owner->name : "API"));
			return place;
		}
		place.where = Place::Register;
		place.kind = kind;
		place.constant = true;
		if (value) {
			place.index = expression(value, kind);
		} else {
			place.index = engine_reg(kind, type);
			emit(Op::HostConstant, p_position, place.index, klass, name);
		}
		return place;
	}

	// A property or field of an engine object.
	Place engine_place(Symbol *p_symbol, Expr *p_object, uint32_t p_position) {
		Place place;
		uint32_t name = 0;
		Kind kind = Kind::F64;
		bool of_singleton = p_symbol->is_static() && p_symbol->kind == SymbolKind::Property && is_singleton(p_symbol->owner);
		if (of_singleton && p_symbol->name == "singleton") {
			// The singleton itself.
			place.where = Place::Register;
			place.index = singleton(p_symbol->owner, p_position);
			place.kind = Kind::Ref;
			place.constant = true;
			return place;
		}
		if (p_symbol->is_static() && !of_singleton) {
			return constant_place(p_symbol, p_position);
		}
		if (!engine_name(p_symbol->owner, p_symbol->name, &name)) {
			refuse(p_position, "'" + std::string(p_symbol->name) + "' of Godot's " + std::string(p_symbol->owner ? p_symbol->owner->name : "API"));
			return place;
		}
		// A property's type is what its getter returns, or what its setter takes.
		TypeRef *type = p_symbol->type;
		if (p_symbol->kind == SymbolKind::Property) {
			type = p_symbol->getter ? p_symbol->getter->return_type
					: p_symbol->setter && p_symbol->setter->parameters.count ? p_symbol->setter->parameters[0].type
																			   : nullptr;
		}
		if (!type || !kind_of(compilation_.type_of(type), &kind) || kind == Kind::Void) {
			refuse(p_position, "a Godot member of type '" + compilation_.type_name(compilation_.type_of(type)) + "'");
			return place;
		}
		place.where = Place::Engine;
		place.index = name;
		place.kind = kind;
		place.type = compilation_.type_of(type);
		if (of_singleton) {
			place.object = singleton(p_symbol->owner, p_position);
			return place;
		}
		Kind whole = Kind::Void;
		if (p_object && (p_object->kind == NodeKind::Name || p_object->kind == NodeKind::Member) && kind_of(p_object->type, &whole) &&
				whole == Kind::Val) {
			// A member of a value that is itself stored somewhere.
			place.of_value = true;
			place.part = std::make_shared<Part>();
			place.part->whole = place_of(p_object);
			whole_of(place, p_position);
			return place;
		}
		place.object = engine_receiver(p_object, p_position, &place.of_value);
		return place;
	}

	Place place_of_symbol(Symbol *p_symbol, uint32_t p_object, uint32_t p_position) {
		Place place;
		if (!p_symbol) {
			refuse(p_position, "this name");
			return place;
		}
		auto local = locals_.find(p_symbol);
		if (local != locals_.end()) {
			place.where = Place::Register;
			place.index = local->second;
			place.kind = kind_at(local->second);
			return place;
		}
		auto global = globals_.find(p_symbol);
		if (global != globals_.end()) {
			if (p_symbol->owner) {
				touch(p_symbol->owner, p_position);
			}
			place.where = Place::GlobalSlot;
			place.index = global->second;
			place.kind = module_.globals[global->second].kind;
			return place;
		}
		auto field = fields_.find(p_symbol);
		if (field != fields_.end() && p_symbol->owner) {
			uint32_t owner = 0;
			if (class_index(p_symbol->owner, &owner)) {
				place.where = Place::FieldOf;
				place.index = field->second;
				place.object = p_object;
				place.kind = module_.classes[owner].fields[field->second].kind;
				return place;
			}
		}
		refuse(p_position, p_symbol->kind == SymbolKind::Property ? "a getter or setter"
																  : "'" + std::string(p_symbol->name) + "' used as a value");
		return place;
	}

	// Evaluates what is needed to reach the place: the object of `a.b`, once.
	Place place_of(Expr *p_target, Expr *p_later = nullptr) {
		if (p_target->kind == NodeKind::Name) {
			Name *name = static_cast<Name *>(p_target);
			if (name->symbol && is_godot(name->symbol) && (name->symbol->kind == SymbolKind::Property || name->symbol->kind == SymbolKind::Field)) {
				return engine_place(name->symbol, nullptr, name->offset);
			}
			if (name->symbol && name->symbol->is_instance_member() && fn().is_static) {
				refuse(name->offset, "this name");
				return Place();
			}
			return place_of_symbol(name->symbol, 0, name->offset);
		}
		if (p_target->kind == NodeKind::Member) {
			Member *member = static_cast<Member *>(p_target);
			Symbol *symbol = member->symbol ? member->symbol : member->member;
			if (symbol && is_godot(symbol) && (symbol->kind == SymbolKind::Property || symbol->kind == SymbolKind::Field)) {
				return engine_place(symbol, member->object, member->name_offset);
			}
			if (symbol && (globals_.count(symbol) || !symbol->is_instance_member())) {
				return place_of_symbol(symbol, 0, member->name_offset);
			}
			// Is the class known before the object is evaluated? A String's `length` is not a field.
			if (symbol && !fields_.count(symbol)) {
				refuse(member->name_offset, symbol->kind == SymbolKind::Property ? "a getter or setter"
																			  : "'" + std::string(symbol->name) + "' used as a value");
				return Place();
			}
			uint32_t object = settled(receiver(member->object), p_later, member->name_offset);
			return place_of_symbol(symbol, object, member->name_offset);
		}
		if (p_target->kind == NodeKind::Index) {
			// An element of one of Godot's values: `vector[0]`.
			Index *index = static_cast<Index *>(p_target);
			Kind whole = Kind::Void;
			if (kind_of(index->object->type, &whole) && whole == Kind::Val) {
				Place place;
				if (!kind_of(p_target->type, &place.kind) || place.kind == Kind::Void) {
					refuse(p_target->offset, "an element of type '" + compilation_.type_name(p_target->type) + "'");
					return Place();
				}
				place.where = Place::Engine;
				place.type = p_target->type;
				place.of_value = true;
				if (index->object->kind == NodeKind::Name || index->object->kind == NodeKind::Member) {
					place.part = std::make_shared<Part>();
					place.part->whole = place_of(index->object);
					whole_of(place, p_target->offset);
				} else {
					place.object = settled(expression(index->object, Kind::Void), index->index, p_target->offset);
				}
				place.key = settled(expression(index->index, Kind::Void), p_later, p_target->offset);
				if (kind_at(place.key) == Kind::Obj) {
					refuse(index->index->offset, "a script object as the key of a Godot value");
				}
				return place;
			}
			refuse(p_target->offset, "indexing with '[]'");
			return Place();
		}
		refuse(p_target->offset, "this kind of assignment target");
		return Place();
	}

	uint32_t load(const Place &p_place, uint32_t p_position) {
		switch (p_place.where) {
			case Place::Register:
				return p_place.index;
			case Place::FieldOf: {
				uint32_t out = reg(p_place.kind);
				emit(Op::GetField, p_position, out, p_place.object, p_place.index);
				return out;
			}
			case Place::GlobalSlot: {
				uint32_t out = reg(p_place.kind);
				emit(Op::GetGlobal, p_position, out, p_place.index);
				return out;
			}
			case Place::Engine: {
				uint32_t object = p_place.part ? whole_of(p_place, p_position) : p_place.object;
				uint32_t out = engine_reg(p_place.kind, p_place.type);
				if (p_place.key != NONE) {
					emit(Op::HostIndexGet, p_position, out, object, p_place.key);
				} else {
					emit(Op::HostGet, p_position, out, object, p_place.index);
				}
				return out;
			}
			default:
				return default_value(Kind::F64, p_position);
		}
	}

	void store(const Place &p_place, uint32_t p_value, uint32_t p_position) {
		switch (p_place.where) {
			case Place::Register:
				if (p_place.constant) {
					refuse(p_position, "changing a constant of Godot's");
					break;
				}
				if (p_place.index != p_value) {
					emit(Op::Move, p_position, p_place.index, p_value);
				}
				break;
			case Place::FieldOf:
				emit(Op::SetField, p_position, p_place.object, p_place.index, p_value);
				break;
			case Place::GlobalSlot:
				emit(Op::SetGlobal, p_position, p_place.index, p_value);
				break;
			case Place::Engine:
				if (p_place.part) {
					uint32_t whole = whole_of(p_place, p_position);
					if (p_place.part->whole.constant) {
						refuse(p_position, "changing one part of a constant of Godot's");
						break;
					}
					if (p_place.key != NONE) {
						emit(Op::HostIndexSet, p_position, whole, p_place.key, p_value);
					} else {
						emit(Op::HostSet, p_position, whole, p_place.index, p_value);
					}
					store(p_place.part->whole, whole, p_position);
				} else if (p_place.of_value) {
					// `a.normalized().x = 5` would change a copy that nobody keeps.
					refuse(p_position, "changing one part of a Godot value that is not stored anywhere");
				} else {
					emit(Op::HostSet, p_position, p_place.object, p_place.index, p_value);
				}
				break;
			default:
				break;
		}
	}

	// The class a `new`, `is`, `as` or conversion names.
	Symbol *named_class(Expr *p_expression) {
		Symbol *symbol = callee_symbol(p_expression);
		return symbol && symbol->is_type() ? symbol : nullptr;
	}

	bool kind_of_class(const Symbol *p_class, Kind *r_kind) {
		if (p_class == int_) {
			*r_kind = Kind::I32;
		} else if (p_class == uint_) {
			*r_kind = Kind::U32;
		} else if (p_class == int64_) {
			*r_kind = Kind::I64;
		} else if (p_class == number_) {
			*r_kind = Kind::F64;
		} else if (p_class == boolean_) {
			*r_kind = Kind::Bool;
		} else if (p_class == string_) {
			*r_kind = Kind::Str;
		} else {
			return false;
		}
		return true;
	}

	// The arguments of a call as the function's parameters expect them, defaults filled in.
	void call_arguments(FunctionDecl *p_function, const List<Expr *> &p_arguments, uint32_t p_position,
			std::vector<uint32_t> &r_list) {
		uint32_t count = p_function ? p_function->parameters.count : 0;
		if (p_arguments.count > count) {
			refuse(p_position, "a call with extra arguments");
		}
		for (uint32_t i = 0; i < count; i++) {
			Parameter &parameter = p_function->parameters[i];
			if (parameter.is_rest) {
				refuse(p_position, "a call to a function with a rest parameter");
				break;
			}
			Kind kind = Kind::F64;
			const Type *type = compilation_.type_of(parameter.type);
			if (!parameter.type || !kind_of(type, &kind)) {
				refuse(p_position, "a call to a function with a parameter of type '" + compilation_.type_name(type) + "'");
				break;
			}
			if (i >= p_arguments.count) {
				break; // Left out: the function that is called supplies its own default.
			}
			Expr *later = nullptr;
			for (uint32_t next = i + 1; next < p_arguments.count && !later; next++) {
				later = writes_locals(p_arguments[next]) ? p_arguments[next] : nullptr;
			}
			r_list.push_back(settled(expression(p_arguments[i], kind), later, p_position));
		}
	}

	// The first of the expressions that can change a local variable, or null.
	static Expr *writer(const List<Expr *> &p_expressions) {
		for (Expr *expression : p_expressions) {
			if (writes_locals(expression)) {
				return expression;
			}
		}
		return nullptr;
	}

	uint32_t call_result(Symbol *p_function, uint32_t p_position, Kind *r_kind) {
		Kind kind = Kind::Void;
		if (p_function->type && !kind_of(compilation_.type_of(p_function->type), &kind)) {
			refuse(p_position, "a call to a function that returns '" + compilation_.type_name(compilation_.type_of(p_function->type)) + "'");
		}
		*r_kind = kind;
		return kind == Kind::Void ? NONE : reg(kind);
	}

	uint32_t call(Call *p_call, Kind p_want) {
		Expr *callee = p_call->callee;
		uint32_t position = p_call->offset;
		// super(...): the base class's constructor.
		if (callee->kind == NodeKind::SuperExpr) {
			Symbol *base = class_ && class_->klass ? class_->klass->base : nullptr;
			uint32_t base_index = 0;
			if (base && base != object_ && class_index(base, &base_index)) {
				std::vector<uint32_t> list = { 0 };
				constructor_arguments(base, p_call->arguments, position, list);
				emit(Op::Call, position, NONE, module_.classes[base_index].constructor, arguments(std::move(list)));
			}
			return 0; // No value: the checker lets nothing use it.
		}
		Symbol *symbol = callee_symbol(callee);
		if (!symbol && callee->kind == NodeKind::Member && signal_ && signal_->klass) {
			// `timer[name].add(f)`: the checker can't know what `timer[name]` is. A signal's method written
			// after it says a signal is meant; the engine checks that it is one when the line runs.
			Member *member = static_cast<Member *>(callee);
			auto verb = signal_->klass->members.find(member->name);
			if (verb != signal_->klass->members.end() && verb->second->kind == SymbolKind::Method) {
				uint32_t object = 0, name = 0;
				uint32_t found = signal_by_name(member->object, writer(p_call->arguments), &object, &name);
				if (found != NONE) {
					return engine_call(p_call, verb->second, found, [&]() { emit(Op::HostMember, member->object->offset, found, object, name); });
				}
			}
		}
		if (!symbol) {
			refuse(callee->offset, "calling a value");
			return default_value(p_want, position);
		}
		// A conversion written as a call: int(x), String(x).
		Kind conversion = Kind::Void;
		if (symbol->is_type()) {
			if (kind_of_class(symbol, &conversion) && p_call->arguments.count == 1) {
				Kind given = Kind::Void;
				bool known = kind_of(p_call->arguments[0]->type, &given);
				// To or from an Int64, a conversion that is written out is AS3's own: see convert().
				uint32_t value = known && (given == Kind::I64 || conversion == Kind::I64) && !is_null(p_call->arguments[0])
						? convert(wide_operand(p_call->arguments[0], conversion), conversion, position, true)
						: expression(p_call->arguments[0], conversion);
				if (conversion == Kind::Str) {
					// The conversion written out always gives a string: a missing one becomes "null".
					uint32_t text = reg(Kind::Str);
					emit(Op::StrToStr, position, text, value);
					return text;
				}
				return value;
			}
			refuse(callee->offset, "a cast to '" + std::string(symbol->name) + "'");
			return default_value(p_want, position);
		}
		if (symbol->unit && symbol->unit->is_builtin && symbol->kind == SymbolKind::Function && symbol->name == "trace") {
			std::vector<uint32_t> list;
			for (uint32_t i = 0; i < p_call->arguments.count; i++) {
				Expr *later = nullptr;
				for (uint32_t next = i + 1; next < p_call->arguments.count && !later; next++) {
					later = writes_locals(p_call->arguments[next]) ? p_call->arguments[next] : nullptr;
				}
				uint32_t value = expression(p_call->arguments[i], Kind::Str);
				// A null string traces as "null".
				uint32_t text = reg(Kind::Str);
				emit(Op::StrToStr, p_call->arguments[i]->offset, text, value);
				list.push_back(settled(text, later, position));
			}
			emit(Op::Trace, position, arguments(std::move(list)));
			return 0;
		}
		if (symbol->name == "toString" && p_call->arguments.count == 0 && callee->kind == NodeKind::Member &&
				static_cast<Member *>(callee)->object->kind != NodeKind::SuperExpr && is_engine(natural(static_cast<Member *>(callee)->object))) {
			// AS3's own toString() on one of Godot's values or objects: Godot's text for it.
			uint32_t value = expression(static_cast<Member *>(callee)->object, Kind::Void);
			emit(Op::NullCheck, position, value);
			uint32_t out = reg(Kind::Str);
			emit(Op::HostToStr, position, out, value);
			return out;
		}
		if (is_godot(symbol) && symbol->kind == SymbolKind::Method && symbol->is_static()) {
			// `Input.isActionPressed(...)`: a method of the engine's one Input. Any other is called on no object.
			return is_singleton(symbol->owner) ? engine_call(p_call, symbol, singleton(symbol->owner, position)) : static_call(p_call, symbol);
		}
		if (is_godot(symbol) && symbol->kind == SymbolKind::Method && !symbol->is_static()) {
			bool through_super = callee->kind == NodeKind::Member && static_cast<Member *>(callee)->object->kind == NodeKind::SuperExpr;
			if (through_super && !(symbol->modifiers & Modifiers::Final)) {
				// Godot's own version of a virtual does nothing. Asking the engine for it would come straight
				// back to the script. The arguments are still evaluated.
				for (Expr *argument : p_call->arguments) {
					expression(argument, Kind::Void);
				}
				Kind kind = Kind::Void;
				if (symbol->type) {
					kind_of(compilation_.type_of(symbol->type), &kind);
				}
				return kind == Kind::Void ? 0 : default_value(kind, position);
			}
			return engine_call(p_call, symbol);
		}
		auto function = functions_.find(symbol);
		if (function == functions_.end() || !symbol->node || symbol->node->kind != NodeKind::FunctionDecl) {
			refuse(callee->offset, "calling '" + std::string(symbol->name) + "'");
			return default_value(p_want, position);
		}
		FunctionDecl *decl = static_cast<FunctionDecl *>(symbol->node);
		std::vector<uint32_t> list;
		bool direct = true;
		bool on_another = false; // Called on something other than `this`: it may be missing.
		if (symbol->kind == SymbolKind::Method && !symbol->is_static()) {
			direct = (symbol->modifiers & Modifiers::Private) != 0;
			if (callee->kind == NodeKind::Member) {
				Expr *object = static_cast<Member *>(callee)->object;
				on_another = object->kind != NodeKind::ThisExpr && object->kind != NodeKind::SuperExpr;
				direct = direct || object->kind == NodeKind::SuperExpr;
				list.push_back(settled(receiver(object), writer(p_call->arguments), position));
			} else {
				if (fn().is_static) {
					refuse(callee->offset, "this call");
				}
				list.push_back(0);
			}
		}
		call_arguments(decl, p_call->arguments, position, list);
		if (on_another) {
			emit(Op::NullCheck, position, list[0]); // After the arguments.
		}
		if (symbol->is_static() && symbol->owner) {
			touch(symbol->owner, position);
		}
		Kind kind = Kind::Void;
		uint32_t out = call_result(symbol, position, &kind);
		if (direct) {
			emit(Op::Call, position, out, function->second, arguments(std::move(list)));
		} else {
			emit(Op::CallVirtual, position, out, slots_[symbol], arguments(std::move(list)));
		}
		return out == NONE ? 0 : out;
	}

	// A method written where Godot takes a function (`onHit`, `this.onHit`, `other.onHit`): the method
	// bound to its object. A private method is that very function; any other is the object's own override,
	// found when the line runs.
	uint32_t bound_method(Expr *p_expression) {
		uint32_t position = p_expression->offset;
		if (is_null(p_expression)) {
			return default_value(Kind::Val, position);
		}
		Symbol *symbol = callee_symbol(p_expression);
		auto function = functions_.find(symbol);
		if (!symbol || symbol->kind != SymbolKind::Method || symbol->is_static() || function == functions_.end()) {
			refuse(position, "a function used as a value, other than a method of a script object");
			return default_value(Kind::Val, position);
		}
		uint32_t object = 0;
		bool on_this = true;
		if (p_expression->kind == NodeKind::Member) {
			Expr *written = static_cast<Member *>(p_expression)->object;
			if (written->kind == NodeKind::SuperExpr) {
				refuse(position, "a method of 'super' used as a value");
				return default_value(Kind::Val, position);
			}
			if (written->kind != NodeKind::ThisExpr) {
				on_this = false;
				object = receiver(written);
				emit(Op::NullCheck, position, object);
			}
		}
		if (on_this && fn().is_static) {
			refuse(position, "this name");
			return default_value(Kind::Val, position);
		}
		bool exact = (symbol->modifiers & Modifiers::Private) != 0;
		uint32_t out = reg(Kind::Val);
		emit(Op::HostBind, position, out, object, exact ? function->second : (slots_[symbol] | ir::BIND_SLOT));
		return out;
	}

	// `object[name]` where a signal's member follows: the signal, by its AS3 name. NONE if this is not one.
	// The object and the name are worked out here; the register is filled by a HostMember that the caller
	// emits once the call's arguments are worked out, so that a wrong name fails after them, as a wrong
	// member does in AS3.
	uint32_t signal_by_name(Expr *p_expression, Expr *p_later, uint32_t *r_object, uint32_t *r_name) {
		if (p_expression->kind != NodeKind::Index) {
			return NONE;
		}
		Index *index = static_cast<Index *>(p_expression);
		Kind object = Kind::Void;
		if (!kind_of(index->object->type, &object) || !is_string_type(index->index->type)) {
			return NONE;
		}
		// A Godot object, or a script object, which stands for the Godot object it is attached to.
		uint32_t klass = 0;
		const Type *type = index->object->type;
		bool script = object == Kind::Obj && type && type->kind == Type::Instance && class_index(type->symbol, &klass) &&
				!module_.classes[klass].native_base.empty();
		if (object != Kind::Ref && !script) {
			return NONE;
		}
		// `this` goes as itself: the name may be one of the script's own fields that holds a signal.
		// Another script object goes as the Godot object behind it: its fields are its own business.
		bool of_value = false;
		uint32_t object_reg = script && index->object->kind == NodeKind::ThisExpr && !fn().is_static
				? 0u
				: engine_receiver(index->object, p_expression->offset, &of_value);
		*r_object = settled(object_reg, index->index, p_expression->offset);
		*r_object = settled(*r_object, p_later, p_expression->offset);
		*r_name = settled(expression(index->index, Kind::Str), p_later, p_expression->offset);
		uint32_t out = reg(Kind::Val);
		if (fn().engine_types.size() <= out) {
			fn().engine_types.resize(out + 1);
		}
		fn().engine_types[out] = "Signal";
		return out;
	}

	// Arguments for the engine: each as its parameter says, or as it comes where the parameter takes anything.
	void engine_arguments(FunctionDecl *p_function, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list) {
		uint32_t count = p_function ? p_function->parameters.count : 0;
		// A default that AS3 can't write is declared `= null`, and Godot fills in the real one when the
		// argument is left out. A `null` written last for such a parameter is the same as leaving it out.
		uint32_t written = p_arguments.count;
		while (written > 0 && written <= count && is_null(p_arguments[written - 1]) && p_function->parameters[written - 1].default_value &&
				is_null(p_function->parameters[written - 1].default_value)) {
			written--;
		}
		for (uint32_t i = 0; i < written; i++) {
			Kind kind = Kind::Void;
			if (i < count && !p_function->parameters[i].is_rest && p_function->parameters[i].type) {
				const Type *type = compilation_.type_of(p_function->parameters[i].type);
				if (type && type->kind == Type::Instance && type->symbol == function_ && function_) {
					// Godot takes a function here: a method goes bound to its object.
					Expr *later = nullptr;
					for (uint32_t next = i + 1; next < written && !later; next++) {
						later = writes_locals(p_arguments[next]) ? p_arguments[next] : nullptr;
					}
					r_list.push_back(settled(bound_method(p_arguments[i]), later, p_position));
					continue;
				}
				if (type && type->kind != Type::Any && !kind_of(type, &kind)) {
					refuse(p_arguments[i]->offset, "passing a value of type '" + compilation_.type_name(type) + "' to Godot");
					kind = Kind::Void;
				}
			}
			Expr *later = nullptr;
			for (uint32_t next = i + 1; next < p_arguments.count && !later; next++) {
				later = writes_locals(p_arguments[next]) ? p_arguments[next] : nullptr;
			}
			if (kind == Kind::Void && is_null(p_arguments[i])) {
				kind = Kind::Ref; // `null` where Godot takes anything: nothing.
			}
			uint32_t value = expression(p_arguments[i], kind);
			if (kind_at(value) == Kind::Obj) {
				refuse(p_arguments[i]->offset, "passing a script object to Godot");
			}
			r_list.push_back(settled(value, later, p_position));
		}
	}

	uint32_t engine_call(Call *p_call, Symbol *p_method, uint32_t p_receiver = NONE, const std::function<void()> &p_before = nullptr) {
		uint32_t position = p_call->offset;
		uint32_t name = 0;
		if (!engine_name(p_method->owner, p_method->name, &name)) {
			refuse(p_call->callee->offset, "calling '" + std::string(p_method->name) + "'");
			return 0;
		}
		Kind kind = Kind::Void;
		if (p_method->type && !kind_of(compilation_.type_of(p_method->type), &kind)) {
			refuse(position, "a call to a Godot method that returns '" + compilation_.type_name(compilation_.type_of(p_method->type)) + "'");
		}
		bool of_value = false;
		Expr *object = p_call->callee->kind == NodeKind::Member ? static_cast<Member *>(p_call->callee)->object : nullptr;
		std::vector<uint32_t> list;
		list.push_back(settled(p_receiver != NONE ? p_receiver : engine_receiver(object, position, &of_value), writer(p_call->arguments), position));
		FunctionDecl *decl = p_method->node && p_method->node->kind == NodeKind::FunctionDecl ? static_cast<FunctionDecl *>(p_method->node) : nullptr;
		engine_arguments(decl, p_call->arguments, position, list);
		if (p_before) {
			p_before(); // What the receiver still needs, now that the arguments are worked out.
		}
		uint32_t out = kind == Kind::Void ? NONE : engine_reg(kind, p_method->type ? compilation_.type_of(p_method->type) : nullptr);
		emit(Op::HostCall, position, out, name, arguments(std::move(list)));
		return out == NONE ? 0 : out;
	}

	// A method of Godot's that is called on no object: `Vector2.fromAngle(a)`.
	uint32_t static_call(Call *p_call, Symbol *p_method) {
		uint32_t position = p_call->offset;
		std::string klass, method;
		if (!p_method->owner || !godot_name(p_method->owner->name, std::string_view(), &klass) ||
				!godot_name(p_method->owner->name, p_method->name, &method)) {
			refuse(p_call->callee->offset, "calling '" + std::string(p_method->name) + "'");
			return 0;
		}
		Kind kind = Kind::Void;
		if (p_method->type && !kind_of(compilation_.type_of(p_method->type), &kind)) {
			refuse(position, "a call to a Godot method that returns '" + compilation_.type_name(compilation_.type_of(p_method->type)) + "'");
		}
		std::vector<uint32_t> list;
		FunctionDecl *decl = p_method->node && p_method->node->kind == NodeKind::FunctionDecl ? static_cast<FunctionDecl *>(p_method->node) : nullptr;
		engine_arguments(decl, p_call->arguments, position, list);
		uint32_t out = kind == Kind::Void ? NONE : engine_reg(kind, p_method->type ? compilation_.type_of(p_method->type) : nullptr);
		emit(Op::HostCallStatic, position, out, string(klass + "." + method), arguments(std::move(list)));
		return out == NONE ? 0 : out;
	}

	void constructor_arguments(Symbol *p_class, const List<Expr *> &p_arguments, uint32_t p_position, std::vector<uint32_t> &r_list) {
		if (is_error(p_class) && p_class->unit->is_builtin) {
			// Error(message = ""): the one parameter the interpreter keeps.
			if (p_arguments.count > 1) {
				refuse(p_position, "an Error with an id");
			}
			if (p_arguments.count >= 1) {
				r_list.push_back(expression(p_arguments[0], Kind::Str));
			} else {
				uint32_t empty = reg(Kind::Str);
				emit(Op::ConstStr, p_position, empty, string(""));
				r_list.push_back(empty);
			}
			return;
		}
		Symbol *constructor = p_class->klass ? p_class->klass->constructor : nullptr;
		FunctionDecl *decl = constructor && constructor->node && constructor->node->kind == NodeKind::FunctionDecl
				? static_cast<FunctionDecl *>(constructor->node)
				: nullptr;
		call_arguments(decl, p_arguments, p_position, r_list);
	}

	uint32_t evaluate(Expr *p_expression, Kind p_want) {
		uint32_t position = p_expression->offset;
		switch (p_expression->kind) {
			case NodeKind::NumberLit: {
				int64_t exact = 0;
				if (p_want == Kind::I64 && wide_literal(p_expression, &exact)) {
					// Digit by digit: a Number would lose the last digits of a large one.
					uint32_t out = reg(Kind::I64);
					emit(Op::ConstI64, position, out, wide(exact));
					return out;
				}
				double value = literal_value(static_cast<NumberLit *>(p_expression));
				Kind kind = natural(p_expression);
				// A literal is written straight into the kind that is wanted, when it fits exactly.
				if ((p_want == Kind::F64 || p_want == Kind::I32 || p_want == Kind::U32) && kind != p_want) {
					bool fits = p_want == Kind::F64 || (value == std::floor(value) && (p_want == Kind::I32 ? value >= -2147483648.0 && value <= 2147483647.0 : value >= 0 && value <= 4294967295.0));
					kind = fits ? p_want : kind;
				}
				uint32_t out = reg(kind);
				if (kind == Kind::I32) {
					emit(Op::ConstI32, position, out, (uint32_t)(int32_t)value);
				} else if (kind == Kind::U32) {
					emit(Op::ConstU32, position, out, (uint32_t)value);
				} else {
					emit(Op::ConstF64, position, out, number(value));
				}
				return out;
			}
			case NodeKind::StringLit: {
				uint32_t out = reg(Kind::Str);
				emit(Op::ConstStr, position, out, string(unescape(static_cast<StringLit *>(p_expression)->text)));
				return out;
			}
			case NodeKind::BoolLit: {
				uint32_t out = reg(Kind::Bool);
				emit(Op::ConstBool, position, out, static_cast<BoolLit *>(p_expression)->value ? 1 : 0);
				return out;
			}
			case NodeKind::ThisExpr:
				if (fn().is_static) {
					refuse(position, "'this' here");
					return default_value(Kind::Obj, position);
				}
				return 0;
			case NodeKind::Name: {
				Name *name = static_cast<Name *>(p_expression);
				uint32_t constant = 0;
				if (builtin_constant(name->symbol, position, &constant)) {
					return constant;
				}
				natural(p_expression);
				return load(place_of(name), position);
			}
			case NodeKind::Member: {
				Member *member = static_cast<Member *>(p_expression);
				Symbol *symbol = member->symbol ? member->symbol : member->member;
				// The one member of a built-in value so far: a string's length.
				if (symbol && symbol->owner == string_ && symbol->name == "length") {
					uint32_t text = expression(member->object, Kind::Str);
					emit(Op::NullCheck, member->object->offset, text);
					uint32_t out = reg(Kind::I32);
					emit(Op::StrLength, position, out, text);
					return out;
				}
				uint32_t constant = 0;
				if (builtin_constant(symbol, position, &constant)) {
					return constant;
				}
				natural(p_expression);
				return load(place_of(member), member->name_offset);
			}
			case NodeKind::Call:
				return call(static_cast<Call *>(p_expression), p_want);
			case NodeKind::New: {
				New *made = static_cast<New *>(p_expression);
				Symbol *symbol = named_class(made->callee);
				uint32_t index = 0;
				uint32_t type_name = 0;
				if (symbol && is_godot(symbol) && !is_godot_object(symbol) && engine_name(symbol, std::string_view(), &type_name)) {
					// One of Godot's value types: the engine makes it.
					Symbol *constructor = symbol->klass ? symbol->klass->constructor : nullptr;
					FunctionDecl *decl = constructor && constructor->node && constructor->node->kind == NodeKind::FunctionDecl
							? static_cast<FunctionDecl *>(constructor->node)
							: nullptr;
					std::vector<uint32_t> list;
					engine_arguments(decl, made->arguments, position, list);
					if (stores_int32(made->type)) {
						// Its constructor takes anything in its first places, so the width is checked here.
						for (size_t i = 0; i < list.size() && i < made->arguments.count; i++) {
							if (is_integral(kind_at(list[i])) || whole(made->arguments[i])) {
								list[i] = as_int32(list[i], made->arguments[i]->offset);
							}
						}
					}
					uint32_t value = reg(Kind::Val);
					emit(Op::HostNew, position, value, type_name, arguments(std::move(list)));
					return value;
				}
				if (!symbol || !class_index(symbol, &index)) {
					refuse(made->callee->offset, symbol ? "creating a '" + std::string(symbol->name) + "'" : std::string("this 'new'"));
					return default_value(Kind::Obj, position);
				}
				if (!module_.classes[index].native_base.empty()) {
					// It would have no Godot object behind it: a script comes to life attached to one.
					refuse(made->callee->offset, "creating a script class with 'new'");
					return default_value(Kind::Obj, position);
				}
				touch(symbol, position);
				uint32_t out = reg(Kind::Obj);
				emit(Op::New, position, out, index);
				std::vector<uint32_t> list = { out };
				constructor_arguments(symbol, made->arguments, position, list);
				emit(Op::Call, position, NONE, module_.classes[index].constructor, arguments(std::move(list)));
				return out;
			}
			case NodeKind::Unary:
				return unary(static_cast<Unary *>(p_expression), p_want);
			case NodeKind::Postfix: {
				Postfix *postfix = static_cast<Postfix *>(p_expression);
				Place place = place_of(postfix->operand);
				if (place.kind == Kind::I64) {
					uint32_t old = reg(Kind::I64);
					emit(Op::Move, position, old, load(place, position));
					uint32_t one = reg(Kind::I64);
					emit(Op::ConstI64, position, one, wide(1));
					uint32_t next = reg(Kind::I64);
					emit(postfix->op == TokenKind::PlusPlus ? Op::AddI64 : Op::SubI64, position, next, old, one);
					store(place, next, position);
					return old;
				}
				// The value is the old one, as a number.
				uint32_t old = convert(load(place, position), Kind::F64, position);
				if (old == place.index && place.where == Place::Register) {
					uint32_t copy = reg(Kind::F64);
					emit(Op::Move, position, copy, old);
					old = copy;
				}
				uint32_t one = reg(Kind::F64);
				emit(Op::ConstF64, position, one, number(1));
				uint32_t next = reg(Kind::F64);
				emit(postfix->op == TokenKind::PlusPlus ? Op::AddF64 : Op::SubF64, position, next, old, one);
				store(place, convert(next, place.kind, position), position);
				return old;
			}
			case NodeKind::Binary:
				return binary(static_cast<Binary *>(p_expression), p_want);
			case NodeKind::Assign:
				return assign(static_cast<Assign *>(p_expression));
			case NodeKind::Conditional: {
				Conditional *conditional = static_cast<Conditional *>(p_expression);
				Kind kind = p_want != Kind::Void ? p_want : natural(p_expression);
				uint32_t out = reg(kind);
				uint32_t test = expression(conditional->condition, Kind::Bool);
				uint32_t to_else = emit(Op::JumpIfNot, position, test, 0);
				uint32_t then_value = expression(conditional->then_value, kind);
				emit(Op::Move, position, out, then_value);
				uint32_t to_end = emit(Op::Jump, position, 0);
				patch(to_else, here());
				uint32_t else_value = expression(conditional->else_value, kind);
				emit(Op::Move, position, out, else_value);
				patch(to_end, here());
				return out;
			}
			case NodeKind::SuperExpr:
				refuse(position, "'super' used as a value");
				break;
			case NodeKind::RegexLit:
				refuse(position, "a regular expression");
				break;
			case NodeKind::ArrayLit:
				refuse(position, "an Array");
				break;
			case NodeKind::ObjectLit:
				refuse(position, "an Object literal");
				break;
			case NodeKind::VectorLit:
				refuse(position, "a Vector");
				break;
			case NodeKind::FunctionExpr:
				refuse(position, "a function used as a value");
				break;
			case NodeKind::Index:
				return load(place_of(p_expression), position);
			case NodeKind::TypeApply:
				refuse(position, "a Vector");
				break;
			default:
				refuse(position, "this expression");
				break;
		}
		return default_value(p_want, position);
	}

	uint32_t unary(Unary *p_unary, Kind p_want) {
		uint32_t position = p_unary->offset;
		(void)p_want;
		switch (p_unary->op) {
			case TokenKind::Minus:
			case TokenKind::Plus: {
				Kind result = Kind::Void;
				if (is_engine(natural(p_unary->operand)) && kind_of(p_unary->type, &result) && result != Kind::Void) {
					return engine_operate(p_unary->op, { operand(p_unary->operand) }, result, p_unary->type, position);
				}
				int64_t exact = 0;
				if (p_want == Kind::I64 && wide_literal(p_unary, &exact)) {
					uint32_t out = reg(Kind::I64);
					emit(Op::ConstI64, position, out, wide(exact)); // The least Int64 has no positive to negate.
					return out;
				}
				if (kind_of(p_unary->operand->type, &result) && result == Kind::I64) {
					uint32_t value = expression(p_unary->operand, Kind::I64);
					if (p_unary->op == TokenKind::Plus) {
						return value;
					}
					uint32_t out = reg(Kind::I64);
					emit(Op::NegI64, position, out, value);
					return out;
				}
				if (p_unary->op == TokenKind::Plus) {
					return expression(p_unary->operand, Kind::F64);
				}
				uint32_t out = reg(Kind::F64);
				emit(Op::NegF64, position, out, expression(p_unary->operand, Kind::F64));
				return out;
			}
			case TokenKind::Bang: {
				uint32_t out = reg(Kind::Bool);
				emit(Op::Not, position, out, expression(p_unary->operand, Kind::Bool));
				return out;
			}
			case TokenKind::Tilde: {
				Kind operand_kind = Kind::Void;
				if (kind_of(p_unary->operand->type, &operand_kind) && operand_kind == Kind::I64) {
					uint32_t out = reg(Kind::I64);
					emit(Op::BitNotI64, position, out, expression(p_unary->operand, Kind::I64));
					return out;
				}
				uint32_t out = reg(Kind::I32);
				emit(Op::BitNot, position, out, expression(p_unary->operand, Kind::I32));
				return out;
			}
			case TokenKind::PlusPlus:
			case TokenKind::MinusMinus: {
				Place place = place_of(p_unary->operand);
				if (place.kind == Kind::I64) {
					uint32_t one = reg(Kind::I64);
					emit(Op::ConstI64, position, one, wide(1));
					uint32_t next = reg(Kind::I64);
					emit(p_unary->op == TokenKind::PlusPlus ? Op::AddI64 : Op::SubI64, position, next, load(place, position), one);
					store(place, next, position);
					return next;
				}
				uint32_t old = convert(load(place, position), Kind::F64, position);
				uint32_t one = reg(Kind::F64);
				emit(Op::ConstF64, position, one, number(1));
				uint32_t next = reg(Kind::F64);
				emit(p_unary->op == TokenKind::PlusPlus ? Op::AddF64 : Op::SubF64, position, next, old, one);
				uint32_t stored = convert(next, place.kind, position);
				store(place, stored, position);
				return stored;
			}
			default:
				refuse(position, "this operator");
				return default_value(Kind::F64, position);
		}
	}

	static bool is_numeric(Kind p_kind) {
		return p_kind == Kind::I32 || p_kind == Kind::U32 || p_kind == Kind::F64 || p_kind == Kind::I64;
	}
	static bool is_integral(Kind p_kind) {
		return p_kind == Kind::I32 || p_kind == Kind::U32 || p_kind == Kind::I64;
	}

	// An operand beside an Int64 (p_beside). A whole number written out is then an Int64 too, read digit
	// by digit, as the checker took it: `w == 9223372036854775807` compares with that very number.
	uint32_t wide_operand(Expr *p_expression, Kind p_beside) {
		int64_t exact = 0;
		if (p_beside == Kind::I64 && wide_literal(p_expression, &exact)) {
			uint32_t out = reg(Kind::I64);
			emit(Op::ConstI64, p_expression->offset, out, wide(exact));
			return out;
		}
		return expression(p_expression, Kind::Void);
	}
	// The kind an operand has for an operator: an Int64 for a whole number written beside one.
	Kind operand_kind(Expr *p_expression, Expr *p_other) {
		Kind kind = Kind::Void, other = Kind::Void;
		kind_of(p_expression->type, &kind);
		int64_t exact = 0;
		if (kind_of(p_other->type, &other) && other == Kind::I64 && wide_literal(p_expression, &exact)) {
			return Kind::I64;
		}
		return kind;
	}

	// An operand for the engine, as the checker typed it: Godot tells a whole number from a fraction
	// (`Vector2i * 2` is a Vector2i), and the interpreter's own arithmetic is all in fractions.
	uint32_t operand(Expr *p_expression, const Type *p_beside = nullptr) {
		if (stores_int32(p_beside) && (whole(p_expression))) {
			// Beside a value of 32-bit integers (`cell * n`): Godot would cut a wider one off.
			return as_int32(expression(p_expression, Kind::Void), p_expression->offset);
		}
		Kind kind = Kind::Void;
		if (!kind_of(p_expression->type, &kind) || !is_numeric(kind)) {
			kind = Kind::Void;
		} else if (kind == Kind::F64 && whole(p_expression)) {
			kind = Kind::I64; // A whole number worked out as a Number: Godot's integer holds every one.
		}
		return expression(p_expression, kind);
	}

	// The checker's rule for a whole number (names_types.cpp, `whole`): an int or uint, or sums,
	// differences, products and remainders of such. AS3 works `i + 1` out as a Number, but the checker
	// gave `cell * (i + 1)` the type Godot gives a whole number, so a whole number is what Godot must get.
	bool whole(const Expr *p_expression) {
		if (!p_expression || !p_expression->type) {
			return false;
		}
		if (p_expression->kind == NodeKind::Binary) {
			const Binary *binary = static_cast<const Binary *>(p_expression);
			switch (binary->op) {
				case TokenKind::Plus:
				case TokenKind::Minus:
				case TokenKind::Star:
				case TokenKind::Percent:
					return whole(binary->left) && whole(binary->right);
				default:
					break;
			}
		}
		if (p_expression->kind == NodeKind::Unary) {
			const Unary *unary = static_cast<const Unary *>(p_expression);
			if (unary->op == TokenKind::Minus || unary->op == TokenKind::Plus) {
				return whole(unary->operand);
			}
		}
		if (p_expression->kind == NodeKind::Conditional) {
			const Conditional *choice = static_cast<const Conditional *>(p_expression);
			return whole(choice->then_value) && whole(choice->else_value);
		}
		if (p_expression->kind == NodeKind::Call) {
			const Call *call = static_cast<const Call *>(p_expression);
			const Symbol *symbol = call->callee && call->callee->kind == NodeKind::Member
					? (static_cast<const Member *>(call->callee)->symbol ? static_cast<const Member *>(call->callee)->symbol : static_cast<const Member *>(call->callee)->member)
					: call->callee && call->callee->kind == NodeKind::Name ? static_cast<const Name *>(call->callee)->symbol
																			: nullptr;
			if (symbol && symbol->unit && symbol->unit->is_builtin) {
				bool math = symbol->owner && symbol->owner->name == "Math" && symbol->kind == SymbolKind::Method;
				if ((math && (symbol->name == "floor" || symbol->name == "round" || symbol->name == "ceil")) ||
						(!symbol->owner && symbol->kind == SymbolKind::Function && symbol->name == "parseInt")) {
					return true;
				}
				if (math && (symbol->name == "abs" || symbol->name == "max" || symbol->name == "min") && call->arguments.count > 0) {
					for (const Expr *argument : call->arguments) {
						if (!whole(argument)) {
							return false;
						}
					}
					return true;
				}
			}
		}
		Kind kind = Kind::Void;
		return kind_of(p_expression->type, &kind) && is_integral(kind);
	}

	// An operator that Godot carries out, because an operand is one of its values. The operator goes as
	// AS3 writes it; `v += w` is `v + w`.
	uint32_t engine_operate(TokenKind p_op, std::vector<uint32_t> p_operands, Kind p_result, const Type *p_type, uint32_t p_position) {
		std::string_view spelling = token_kind_spelling(p_op);
		switch (p_op) {
			case TokenKind::StrictEqual:
				spelling = "==";
				break;
			case TokenKind::StrictNotEqual:
				spelling = "!=";
				break;
			default:
				// A compound assignment is spelled as its operator and '='.
				if (spelling.size() >= 2 && spelling.back() == '=' && p_op != TokenKind::Equal && p_op != TokenKind::NotEqual &&
						p_op != TokenKind::LessEqual && p_op != TokenKind::GreaterEqual) {
					spelling.remove_suffix(1);
				}
				break;
		}
		for (uint32_t operand : p_operands) {
			if (kind_at(operand) == Kind::Obj) {
				refuse(p_position, "an operator between a Godot value and a script object");
			}
		}
		uint32_t out = engine_reg(p_result, p_type);
		emit(Op::HostOperate, p_position, out, string(std::string(spelling)), arguments(std::move(p_operands)));
		return out;
	}

	// An arithmetic, bitwise or string operation on two values already evaluated.
	uint32_t operate(TokenKind p_op, uint32_t p_left, uint32_t p_right, bool p_string, uint32_t p_position) {
		if (p_string) {
			uint32_t left = reg(Kind::Str), right = reg(Kind::Str), out = reg(Kind::Str);
			emit(Op::StrToStr, p_position, left, convert(p_left, Kind::Str, p_position));
			emit(Op::StrToStr, p_position, right, convert(p_right, Kind::Str, p_position));
			emit(Op::Concat, p_position, out, left, right);
			return out;
		}
		if (kind_at(p_left) == Kind::I64 || kind_at(p_right) == Kind::I64) {
			// With an Int64, whole-number arithmetic stays in 64 bits. Division, and anything with a
			// Number, falls through to Numbers: the Int64 converts, and says so if it does not fit.
			Op wide_op = Op::Stmt;
			bool whole_only = true;
			switch (p_op) {
				case TokenKind::Plus:
				case TokenKind::PlusAssign:
					wide_op = Op::AddI64;
					break;
				case TokenKind::Minus:
				case TokenKind::MinusAssign:
					wide_op = Op::SubI64;
					break;
				case TokenKind::Star:
				case TokenKind::StarAssign:
					wide_op = Op::MulI64;
					break;
				case TokenKind::Percent:
				case TokenKind::PercentAssign:
					wide_op = Op::ModI64;
					break;
				case TokenKind::Amp:
				case TokenKind::AmpAssign:
					wide_op = Op::BitAndI64;
					whole_only = false;
					break;
				case TokenKind::Pipe:
				case TokenKind::PipeAssign:
					wide_op = Op::BitOrI64;
					whole_only = false;
					break;
				case TokenKind::Caret:
				case TokenKind::CaretAssign:
					wide_op = Op::BitXorI64;
					whole_only = false;
					break;
				case TokenKind::ShiftLeft:
				case TokenKind::ShiftLeftAssign:
					wide_op = Op::ShlI64;
					whole_only = false;
					break;
				case TokenKind::ShiftRight:
				case TokenKind::ShiftRightAssign:
					wide_op = Op::ShrI64;
					whole_only = false;
					break;
				case TokenKind::ShiftRightUnsigned:
				case TokenKind::ShiftRightUnsignedAssign:
					wide_op = Op::UShrI64;
					whole_only = false;
					break;
				default:
					break;
			}
			if (wide_op != Op::Stmt && (!whole_only || (is_integral(kind_at(p_left)) && is_integral(kind_at(p_right))))) {
				uint32_t left = convert(p_left, Kind::I64, p_position);
				uint32_t right = convert(p_right, Kind::I64, p_position);
				uint32_t out = reg(Kind::I64);
				emit(wide_op, p_position, out, left, right);
				return out;
			}
		}
		Op op = Op::AddF64;
		Kind operands = Kind::F64;
		Kind result = Kind::F64;
		switch (p_op) {
			case TokenKind::Plus:
			case TokenKind::PlusAssign:
				op = Op::AddF64;
				break;
			case TokenKind::Minus:
			case TokenKind::MinusAssign:
				op = Op::SubF64;
				break;
			case TokenKind::Star:
			case TokenKind::StarAssign:
				op = Op::MulF64;
				break;
			case TokenKind::Slash:
			case TokenKind::SlashAssign:
				op = Op::DivF64;
				break;
			case TokenKind::Percent:
			case TokenKind::PercentAssign:
				op = Op::ModF64;
				break;
			case TokenKind::Amp:
			case TokenKind::AmpAssign:
				op = Op::BitAnd;
				operands = result = Kind::I32;
				break;
			case TokenKind::Pipe:
			case TokenKind::PipeAssign:
				op = Op::BitOr;
				operands = result = Kind::I32;
				break;
			case TokenKind::Caret:
			case TokenKind::CaretAssign:
				op = Op::BitXor;
				operands = result = Kind::I32;
				break;
			case TokenKind::ShiftLeft:
			case TokenKind::ShiftLeftAssign:
				op = Op::Shl;
				operands = result = Kind::I32;
				break;
			case TokenKind::ShiftRight:
			case TokenKind::ShiftRightAssign:
				op = Op::Shr;
				operands = result = Kind::I32;
				break;
			case TokenKind::ShiftRightUnsigned:
			case TokenKind::ShiftRightUnsignedAssign:
				op = Op::UShr;
				operands = Kind::I32;
				result = Kind::U32;
				break;
			default:
				refuse(p_position, "this operator");
				break;
		}
		uint32_t out = reg(result);
		uint32_t left = convert(p_left, op == Op::UShr ? Kind::U32 : operands, p_position);
		emit(op, p_position, out, left, convert(p_right, operands, p_position));
		return out;
	}

	bool is_string_type(const Type *p_type) const {
		return p_type && p_type->kind == Type::Instance && p_type->symbol == string_;
	}

	uint32_t binary(Binary *p_binary, Kind p_want) {
		uint32_t position = p_binary->offset;
		Expr *left = p_binary->left;
		Expr *right = p_binary->right;
		switch (p_binary->op) {
			case TokenKind::AmpAmp:
			case TokenKind::PipePipe: {
				// As a condition it is a Boolean; otherwise it is one of its operands, which must then be
				// of one kind.
				Kind kind = Kind::Bool;
				if (p_want != Kind::Bool) {
					Kind a = is_null(left) ? Kind::Void : natural(left);
					Kind b = is_null(right) ? Kind::Void : natural(right);
					if (a != b && is_integral(a) && is_integral(b) && (a == Kind::I64 || b == Kind::I64)) {
						a = b = Kind::I64; // An Int64 holds both, as the checker took it.
					}
					kind = a == Kind::Void ? b : a;
					if (kind == Kind::Void || (a != Kind::Void && b != Kind::Void && a != b)) {
						refuse(position, "'&&' or '||' between values of different types");
						kind = Kind::Bool;
					}
				}
				uint32_t out = reg(kind);
				emit(Op::Move, position, out, expression(left, kind));
				uint32_t test = convert(out, Kind::Bool, position);
				uint32_t skip = emit(p_binary->op == TokenKind::AmpAmp ? Op::JumpIfNot : Op::JumpIf, position, test, 0);
				emit(Op::Move, position, out, expression(right, kind));
				patch(skip, here());
				return out;
			}
			case TokenKind::Comma:
				expression(left, Kind::Void);
				return expression(right, p_want);
			case TokenKind::KwIs:
			case TokenKind::KwAs: {
				Symbol *symbol = named_class(right);
				uint32_t index = 0;
				if (!symbol || !class_index(symbol, &index)) {
					refuse(right->offset, std::string(p_binary->op == TokenKind::KwIs ? "'is'" : "'as'") + " with this type");
					return default_value(p_binary->op == TokenKind::KwIs ? Kind::Bool : Kind::Obj, position);
				}
				uint32_t object = expression(left, Kind::Void);
				if (kind_at(object) != Kind::Obj) {
					refuse(left->offset, std::string(p_binary->op == TokenKind::KwIs ? "'is'" : "'as'") + " on a value that is not an object");
					return default_value(Kind::Bool, position);
				}
				uint32_t out = reg(p_binary->op == TokenKind::KwIs ? Kind::Bool : Kind::Obj);
				emit(p_binary->op == TokenKind::KwIs ? Op::IsClass : Op::AsClass, position, out, object, index);
				return out;
			}
			case TokenKind::Less:
			case TokenKind::Greater:
			case TokenKind::LessEqual:
			case TokenKind::GreaterEqual: {
				if (is_engine(natural(left)) || is_engine(natural(right))) {
					uint32_t a = settled(operand(left), right, position);
					uint32_t b = operand(right);
					return engine_operate(p_binary->op, { a, b }, Kind::Bool, nullptr, position);
				}
				bool swap_sides = p_binary->op == TokenKind::Greater || p_binary->op == TokenKind::GreaterEqual;
				bool with_equal = p_binary->op == TokenKind::LessEqual || p_binary->op == TokenKind::GreaterEqual;
				Kind left_kind = operand_kind(left, right), right_kind = operand_kind(right, left);
				if ((left_kind == Kind::I64 || right_kind == Kind::I64) && is_integral(left_kind) && is_integral(right_kind)) {
					uint32_t a = settled(convert(wide_operand(left, right_kind), Kind::I64, position), right, position);
					uint32_t b = convert(wide_operand(right, left_kind), Kind::I64, position);
					uint32_t out = reg(Kind::Bool);
					emit(with_equal ? Op::LeI64 : Op::LtI64, position, out, swap_sides ? b : a, swap_sides ? a : b);
					return out;
				}
				if ((left_kind == Kind::I64 && right_kind == Kind::F64) || (left_kind == Kind::F64 && right_kind == Kind::I64)) {
					// Exactly: neither side is converted to the other's type.
					uint32_t a = settled(expression(left, Kind::Void), right, position);
					uint32_t b = expression(right, Kind::Void);
					bool wide_first = left_kind == Kind::I64;
					uint32_t order = reg(Kind::F64);
					emit(Op::CmpI64F64, position, order, wide_first ? a : b, wide_first ? b : a);
					uint32_t zero = reg(Kind::F64);
					emit(Op::ConstF64, position, zero, number(0));
					// `order` places the Int64 against the Number. Not-a-number makes every comparison false.
					bool wide_is_less = wide_first != swap_sides; // The question is "Int64 < Number" (or <=).
					uint32_t out = reg(Kind::Bool);
					emit(with_equal ? Op::LeF64 : Op::LtF64, position, out, wide_is_less ? order : zero, wide_is_less ? zero : order);
					return out;
				}
				bool strings = is_string_type(left->type) && is_string_type(right->type);
				Kind kind = strings ? Kind::Str : Kind::F64;
				uint32_t a = settled(expression(left, kind), right, position);
				uint32_t b = expression(right, kind);
				bool swap = p_binary->op == TokenKind::Greater || p_binary->op == TokenKind::GreaterEqual;
				bool or_equal = p_binary->op == TokenKind::LessEqual || p_binary->op == TokenKind::GreaterEqual;
				uint32_t out = reg(Kind::Bool);
				emit(strings ? (or_equal ? Op::LeStr : Op::LtStr) : (or_equal ? Op::LeF64 : Op::LtF64), position, out, swap ? b : a,
						swap ? a : b);
				return out;
			}
			case TokenKind::Equal:
			case TokenKind::NotEqual:
			case TokenKind::StrictEqual:
			case TokenKind::StrictNotEqual: {
				Kind a = is_null(left) ? Kind::Void : natural(left);
				Kind b = is_null(right) ? Kind::Void : natural(right);
				bool strict = p_binary->op == TokenKind::StrictEqual || p_binary->op == TokenKind::StrictNotEqual;
				Kind kind = Kind::F64;
				Op op = Op::EqF64;
				if (a == Kind::Void && b == Kind::Void) {
					kind = Kind::Obj;
					op = Op::EqRef;
				} else if (a == Kind::Void || b == Kind::Void) {
					// Against null: a reference is compared; a number or a Boolean is never null.
					kind = a == Kind::Void ? b : a;
					if (is_engine(kind)) {
						// A Godot object or value against null: is there one?
						uint32_t object = expression(a == Kind::Void ? right : left, kind);
						uint32_t present = reg(Kind::Bool);
						emit(Op::ObjToBool, position, present, object);
						if (p_binary->op == TokenKind::NotEqual || p_binary->op == TokenKind::StrictNotEqual) {
							return present;
						}
						uint32_t absent = reg(Kind::Bool);
						emit(Op::Not, position, absent, present);
						return absent;
					}
					if (!is_reference(kind)) {
						refuse(position, "comparing a number or a Boolean with null");
					}
					op = kind == Kind::Str ? Op::EqStr : Op::EqRef;
				} else if (is_engine(a) || is_engine(b)) {
					// Godot's own equality: the same object, or equal values.
					uint32_t x = settled(operand(left), right, position);
					uint32_t y = operand(right);
					return engine_operate(p_binary->op, { x, y }, Kind::Bool, nullptr, position);
				} else if (a == Kind::Str && b == Kind::Str) {
					kind = Kind::Str;
					op = Op::EqStr;
				} else if (a == Kind::Obj && b == Kind::Obj) {
					kind = Kind::Obj;
					op = Op::EqRef;
				} else if (a == Kind::Bool && b == Kind::Bool) {
					kind = Kind::Bool;
					op = Op::EqBool;
				} else if ((operand_kind(left, right) == Kind::I64 || operand_kind(right, left) == Kind::I64) &&
						is_numeric(operand_kind(left, right)) && is_numeric(operand_kind(right, left))) {
					Kind x_kind = operand_kind(left, right), y_kind = operand_kind(right, left);
					uint32_t out = reg(Kind::Bool);
					if (is_integral(x_kind) && is_integral(y_kind)) {
						uint32_t x = settled(convert(wide_operand(left, y_kind), Kind::I64, position), right, position);
						uint32_t y = convert(wide_operand(right, x_kind), Kind::I64, position);
						emit(Op::EqI64, position, out, x, y);
					} else {
						// An Int64 and a Number, exactly.
						uint32_t x = settled(expression(left, Kind::Void), right, position);
						uint32_t y = expression(right, Kind::Void);
						uint32_t order = reg(Kind::F64);
						emit(Op::CmpI64F64, position, order, x_kind == Kind::I64 ? x : y, x_kind == Kind::I64 ? y : x);
						uint32_t zero = reg(Kind::F64);
						emit(Op::ConstF64, position, zero, number(0));
						emit(Op::EqF64, position, out, order, zero);
					}
					if (p_binary->op == TokenKind::NotEqual || p_binary->op == TokenKind::StrictNotEqual) {
						uint32_t negated = reg(Kind::Bool);
						emit(Op::Not, position, negated, out);
						return negated;
					}
					return out;
				} else if ((is_numeric(a) && is_numeric(b)) || (!strict && (is_numeric(a) || a == Kind::Bool) && (is_numeric(b) || b == Kind::Bool))) {
					kind = Kind::F64;
				} else {
					refuse(position, "comparing values of these two types");
				}
				uint32_t x = settled(expression(left, kind), right, position);
				uint32_t y = expression(right, kind);
				uint32_t out = reg(Kind::Bool);
				emit(op, position, out, x, y);
				if (p_binary->op == TokenKind::NotEqual || p_binary->op == TokenKind::StrictNotEqual) {
					uint32_t negated = reg(Kind::Bool);
					emit(Op::Not, position, negated, out);
					return negated;
				}
				return out;
			}
			case TokenKind::KwIn:
			case TokenKind::KwInstanceof:
				refuse(position, p_binary->op == TokenKind::KwIn ? "'in'" : "'instanceof'");
				return default_value(Kind::Bool, position);
			default: {
				Kind left_kind = Kind::Void, right_kind = Kind::Void;
				kind_of(left->type, &left_kind);
				kind_of(right->type, &right_kind);
				bool strings = p_binary->op == TokenKind::Plus && is_string_type(p_binary->type);
				if (!strings && (is_engine(left_kind) || is_engine(right_kind))) {
					Kind result = Kind::Void;
					if (!kind_of(p_binary->type, &result) || result == Kind::Void) {
						refuse(position, "this operator on a Godot value");
						return default_value(p_want, position);
					}
					uint32_t a = settled(operand(left, right->type), right, position);
					uint32_t b = operand(right, left->type);
					return engine_operate(p_binary->op, { a, b }, result, p_binary->type, position);
				}
				// Division is done in Numbers, so a literal beside an Int64 stays the Number it is written as.
				bool exact = !strings && p_binary->op != TokenKind::Slash;
				uint32_t a = settled(wide_operand(left, exact ? right_kind : Kind::Void), right, position);
				uint32_t b = wide_operand(right, exact ? left_kind : Kind::Void);
				return operate(p_binary->op, a, b, strings, position);
			}
		}
	}

	uint32_t assign(Assign *p_assign) {
		uint32_t position = p_assign->offset;
		Place place = place_of(p_assign->target, p_assign->value);
		uint32_t value = 0;
		if (p_assign->op == TokenKind::Assign) {
			value = expression(p_assign->value, place.kind);
		} else if (p_assign->op == TokenKind::AmpAmpAssign || p_assign->op == TokenKind::PipePipeAssign) {
			refuse(position, "'&&=' or '||='");
			value = default_value(place.kind, position);
		} else {
			uint32_t old = settled(load(place, position), p_assign->value, position);
			bool strings = p_assign->op == TokenKind::PlusAssign && (place.kind == Kind::Str || is_string_type(p_assign->value->type));
			Kind right_kind = Kind::Void;
			kind_of(p_assign->value->type, &right_kind);
			bool engine = !strings && (is_engine(kind_at(old)) || is_engine(right_kind));
			uint32_t right = engine ? operand(p_assign->value, p_assign->target->type) : wide_operand(p_assign->value, strings || p_assign->op == TokenKind::SlashAssign ? Kind::Void : place.kind);
			if (engine) {
				value = convert(engine_operate(p_assign->op, { old, right }, place.kind, p_assign->target->type, position), place.kind, position);
			} else {
				value = convert(operate(p_assign->op, old, right, strings, position), place.kind, position);
			}
		}
		store(place, value, position);
		return value;
	}

	// ---- statements ----

	void declare_locals(Node *p_body) {
		// A local that is given a value by its one declaration, written straight in the function's body,
		// needs no starting value of its own: nothing below that line can see it without one.
		std::unordered_map<const Symbol *, int> declared;
		std::unordered_set<const Symbol *> valued;
		walk(p_body, [&](Node *p_node) {
			if (p_node->kind == NodeKind::VarDecl) {
				for (Declarator &declarator : static_cast<VarDecl *>(p_node)->declarators) {
					declared[declarator.symbol]++;
				}
			}
		});
		if (p_body && p_body->kind == NodeKind::Block) {
			for (Stmt *statement : static_cast<Block *>(p_body)->statements) {
				if (statement && statement->kind == NodeKind::VarStmt && static_cast<VarStmt *>(statement)->decl) {
					for (Declarator &declarator : static_cast<VarStmt *>(statement)->decl->declarators) {
						if (declarator.initializer && declared[declarator.symbol] == 1) {
							valued.insert(declarator.symbol);
						}
					}
				}
			}
		}
		walk(p_body, [&](Node *p_node) {
			if (p_node->kind != NodeKind::VarDecl) {
				return;
			}
			for (Declarator &declarator : static_cast<VarDecl *>(p_node)->declarators) {
				if (!declarator.symbol || locals_.count(declarator.symbol)) {
					continue;
				}
				Kind kind = written_kind(declarator.type, declarator.offset, Kind::F64);
				uint32_t local = reg(kind);
				locals_[declarator.symbol] = local;
				fn().locals.push_back({ std::string(declarator.name), local });
				// A Number starts as NaN. The rest start as zero, which is what a fresh register holds.
				if (kind == Kind::F64) {
					emit(Op::ConstF64, declarator.offset, local, number(std::numeric_limits<double>::quiet_NaN()));
				} else if (kind == Kind::Val && !valued.count(declarator.symbol)) {
					// One of Godot's values with parts starts at its zero.
					zero_value(declarator.type, declarator.offset, local);
				}
			}
		});
	}

	void statement(Stmt *p_statement) {
		if (!p_statement) {
			return;
		}
		uint32_t position = p_statement->offset;
		bool loop = p_statement->kind == NodeKind::While || p_statement->kind == NodeKind::DoWhile || p_statement->kind == NodeKind::For;
		if (p_statement->kind != NodeKind::Block && p_statement->kind != NodeKind::Empty && !loop) {
			emit(Op::Stmt, position); // A loop marks its condition instead, once for each pass.
		}
		switch (p_statement->kind) {
			case NodeKind::Block:
				for (Stmt *inner : static_cast<Block *>(p_statement)->statements) {
					statement(inner);
				}
				break;
			case NodeKind::Empty:
			case NodeKind::ImportStmt:
				break;
			case NodeKind::ExprStmt:
				expression(static_cast<ExprStmt *>(p_statement)->expr, Kind::Void);
				break;
			case NodeKind::VarStmt:
				variables(static_cast<VarStmt *>(p_statement)->decl);
				break;
			case NodeKind::If: {
				If *branch = static_cast<If *>(p_statement);
				uint32_t test = expression(branch->condition, Kind::Bool);
				uint32_t to_else = emit(Op::JumpIfNot, position, test, 0);
				statement(branch->then_branch);
				if (branch->else_branch) {
					uint32_t to_end = emit(Op::Jump, position, 0);
					patch(to_else, here());
					statement(branch->else_branch);
					patch(to_end, here());
				} else {
					patch(to_else, here());
				}
				break;
			}
			case NodeKind::While: {
				While *loop = static_cast<While *>(p_statement);
				uint32_t top = here();
				emit(Op::Stmt, position);
				uint32_t test = expression(loop->condition, Kind::Bool);
				uint32_t out = emit(Op::JumpIfNot, position, test, 0);
				loops_.emplace_back();
				statement(loop->body);
				emit(Op::Jump, position, top);
				patch(out, here());
				finish_loop(top);
				break;
			}
			case NodeKind::DoWhile: {
				DoWhile *loop = static_cast<DoWhile *>(p_statement);
				uint32_t top = here();
				loops_.emplace_back();
				statement(loop->body);
				uint32_t test_at = here();
				emit(Op::Stmt, loop->condition ? loop->condition->offset : position);
				uint32_t test = expression(loop->condition, Kind::Bool);
				emit(Op::JumpIf, position, test, top);
				finish_loop(test_at);
				break;
			}
			case NodeKind::For: {
				For *loop = static_cast<For *>(p_statement);
				statement(loop->init);
				uint32_t top = here();
				uint32_t out = NONE;
				if (!loop->condition) {
					emit(Op::Stmt, position);
				}
				if (loop->condition) {
					emit(Op::Stmt, loop->condition->offset);
					uint32_t test = expression(loop->condition, Kind::Bool);
					out = emit(Op::JumpIfNot, position, test, 0);
				}
				loops_.emplace_back();
				statement(loop->body);
				uint32_t update_at = here();
				if (loop->update) {
					expression(loop->update, Kind::Void);
				}
				emit(Op::Jump, position, top);
				if (out != NONE) {
					patch(out, here());
				}
				finish_loop(update_at);
				break;
			}
			case NodeKind::Break:
			case NodeKind::Continue: {
				bool is_break = p_statement->kind == NodeKind::Break;
				std::string_view label = is_break ? static_cast<Break *>(p_statement)->label : static_cast<Continue *>(p_statement)->label;
				if (!label.empty()) {
					refuse(position, "a labelled 'break' or 'continue'");
				} else if (!loops_.empty()) {
					uint32_t jump = emit(Op::Jump, position, 0);
					(is_break ? loops_.back().breaks : loops_.back().continues).push_back(jump);
				}
				break;
			}
			case NodeKind::Return: {
				Return *result = static_cast<Return *>(p_statement);
				if (constructor_ || fn().result == Kind::Void) {
					if (result->value) {
						expression(result->value, Kind::Void);
					}
					emit(Op::ReturnVoid, position);
				} else {
					uint32_t value = result->value ? expression(result->value, fn().result) : default_value(fn().result, position);
					emit(Op::Return, position, value);
				}
				break;
			}
			case NodeKind::Throw: {
				uint32_t value = expression(static_cast<Throw *>(p_statement)->value, Kind::Void);
				if (kind_at(value) != Kind::Obj) {
					refuse(position, "throwing a value that is not an object");
				} else {
					emit(Op::Throw, position, value);
				}
				break;
			}
			case NodeKind::Switch:
				refuse(position, "'switch'");
				break;
			case NodeKind::Try:
				refuse(position, "'try'");
				break;
			case NodeKind::ForIn:
				refuse(position, static_cast<ForIn *>(p_statement)->each ? "'for each'" : "'for in'");
				break;
			case NodeKind::Labeled:
				refuse(position, "a label");
				break;
			case NodeKind::FunctionStmt:
				refuse(position, "a function inside a function");
				break;
			default:
				refuse(position, "this statement");
				break;
		}
	}

	void finish_loop(uint32_t p_continue_at) {
		for (uint32_t jump : loops_.back().breaks) {
			patch(jump, here());
		}
		for (uint32_t jump : loops_.back().continues) {
			patch(jump, p_continue_at);
		}
		loops_.pop_back();
	}

	void variables(VarDecl *p_decl) {
		for (Declarator &declarator : p_decl->declarators) {
			auto local = locals_.find(declarator.symbol);
			if (local == locals_.end() || !declarator.initializer) {
				continue; // Without an initializer a `var` keeps what the variable holds: it is declared once.
			}
			uint32_t value = expression(declarator.initializer, kind_at(local->second));
			if (value != local->second) {
				emit(Op::Move, declarator.offset, local->second, value);
			}
		}
	}

	// ---- functions ----

	void begin(uint32_t p_function, Unit &p_unit, Symbol *p_class) {
		function_index_ = p_function;
		unit_ = &p_unit;
		class_ = p_class;
		constructor_ = false;
		locals_.clear();
		loops_.clear();
	}

	// Parameters take the first registers; an instance method's first is `this`.
	void parameters(FunctionDecl *p_function) {
		if (!fn().is_static) {
			fn().registers.push_back(Kind::Obj);
			fn().locals.push_back({ "this", 0 });
		}
		if (p_function) {
			for (Parameter &parameter : p_function->parameters) {
				if (parameter.is_rest) {
					refuse(parameter.offset, "a rest parameter");
				}
				Kind kind = written_kind(parameter.type, parameter.offset, Kind::F64);
				uint32_t local = reg(kind);
				if (is_engine(kind) && parameter.type) {
					// What the engine must be handing over for this parameter.
					const Type *type = compilation_.type_of(parameter.type);
					std::string godot;
					if (type && type->symbol && godot_name(type->symbol->name, std::string_view(), &godot)) {
						fn().engine_types.resize(local + 1);
						fn().engine_types[local] = godot;
					}
				}
				if (parameter.symbol) {
					locals_[parameter.symbol] = local;
				}
				fn().locals.push_back({ std::string(parameter.name), local });
			}
		}
		fn().parameters = (uint32_t)fn().registers.size();
		fn().required = fn().parameters;
		for (uint32_t i = 0; p_function && i < p_function->parameters.count; i++) {
			if (p_function->parameters[i].default_value) {
				fn().required = fn().parameters - (p_function->parameters.count - i);
				break;
			}
		}
		// A parameter the caller left out takes its default here, in the function that runs.
		for (uint32_t i = 0; p_function && i < p_function->parameters.count; i++) {
			Parameter &parameter = p_function->parameters[i];
			if (!parameter.default_value || !parameter.symbol) {
				continue;
			}
			uint32_t local = locals_[parameter.symbol];
			uint32_t missing = reg(Kind::Bool);
			emit(Op::ArgMissing, parameter.offset, missing, local);
			uint32_t skip = emit(Op::JumpIfNot, parameter.offset, missing, 0);
			uint32_t value = expression(parameter.default_value, kind_at(local));
			if (value != local) {
				emit(Op::Move, parameter.offset, local, value);
			}
			patch(skip, here());
		}
	}

	void end(uint32_t p_position) {
		if (fn().result == Kind::Void) {
			emit(Op::ReturnVoid, p_position);
		} else {
			// Falling off the end gives the type's default value.
			emit(Op::Return, p_position, default_value(fn().result, p_position));
		}
	}

	void lower_function(Unit &p_unit, Symbol *p_class, FunctionDecl *p_decl) {
		auto found = functions_.find(p_decl->symbol);
		if (found == functions_.end()) {
			return;
		}
		begin(found->second, p_unit, p_class);
		parameters(p_decl);
		if (p_decl->return_type) {
			fn().result = written_kind(p_decl->return_type, p_decl->return_type->offset, Kind::Void);
		} else {
			unit_ = &p_unit;
			refuse(p_decl->name_offset, "a function without a return type");
		}
		if (p_decl->body) {
			declare_locals(p_decl->body);
			statement(p_decl->body);
		}
		end(p_decl->end ? p_decl->end - 1 : p_decl->offset);
	}

	static bool calls_super(Block *p_body) {
		bool found = false;
		walk(p_body, [&](Node *p_node) {
			found = found || (p_node->kind == NodeKind::Call && static_cast<Call *>(p_node)->callee->kind == NodeKind::SuperExpr);
		});
		return found;
	}

	void lower_constructor(Unit &p_unit, ClassDecl *p_class) {
		Symbol *symbol = p_class->symbol;
		uint32_t index = classes_[symbol];
		Symbol *constructor = symbol->klass->constructor;
		FunctionDecl *decl = constructor && constructor->node && constructor->node->kind == NodeKind::FunctionDecl
				? static_cast<FunctionDecl *>(constructor->node)
				: nullptr;
		begin(module_.classes[index].constructor, p_unit, symbol);
		constructor_ = true;
		parameters(decl);
		if (decl && decl->body) {
			declare_locals(decl->body);
		}
		// The class's own fields get their values first, before any constructor body runs. One of Godot's
		// values with parts starts at its zero, and does so before any written value is worked out, since
		// that may read it.
		for (Node *member : p_class->members) {
			if (member->kind != NodeKind::VarDecl || (static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
				continue;
			}
			for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
				auto field = fields_.find(declarator.symbol);
				if (field != fields_.end() && !declarator.initializer && module_.classes[index].fields[field->second].kind == Kind::Val) {
					uint32_t zero = zero_value(declarator.type, declarator.offset);
					if (zero != NONE) {
						emit(Op::SetField, declarator.offset, 0, field->second, zero);
					}
				}
			}
		}
		for (Node *member : p_class->members) {
			if (member->kind != NodeKind::VarDecl || (static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
				continue;
			}
			for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
				auto field = fields_.find(declarator.symbol);
				if (field != fields_.end() && declarator.initializer) {
					Kind kind = module_.classes[index].fields[field->second].kind;
					emit(Op::SetField, declarator.offset, 0, field->second, expression(declarator.initializer, kind));
				}
			}
		}
		// Then the base class's constructor, unless the body calls it itself.
		Symbol *base = symbol->klass->base;
		uint32_t base_index = 0;
		if (base && base != object_ && class_index(base, &base_index) && !(decl && decl->body && calls_super(decl->body))) {
			std::vector<uint32_t> list = { 0 };
			List<Expr *> none;
			constructor_arguments(base, none, p_class->name_offset, list);
			emit(Op::Call, p_class->name_offset, NONE, module_.classes[base_index].constructor, arguments(std::move(list)));
		}
		if (decl && decl->body) {
			statement(decl->body);
		}
		emit(Op::ReturnVoid, decl && decl->end ? decl->end - 1 : p_class->name_offset);
	}

	// A function that gives the class's static fields their values.
	void lower_statics(Unit &p_unit, ClassDecl *p_class) {
		bool any = false;
		for (Node *member : p_class->members) {
			if (member->kind == NodeKind::VarDecl && (static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
				for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
					auto global = globals_.find(declarator.symbol);
					any = any || (global != globals_.end() && (declarator.initializer || module_.globals[global->second].kind == Kind::Val));
				}
			}
		}
		if (!any) {
			return;
		}
		Function function;
		function.name = module_.classes[classes_[p_class->symbol]].name + "$static";
		function.file = file(&p_unit);
		function.position = p_class->name_offset;
		function.owner = classes_[p_class->symbol];
		function.is_static = true;
		module_.functions.push_back(std::move(function));
		uint32_t index = (uint32_t)module_.functions.size() - 1;
		module_.classes[classes_[p_class->symbol]].static_init = index; // Run on the class's first use.
		begin(index, p_unit, p_class->symbol);
		for (Node *member : p_class->members) {
			if (member->kind != NodeKind::VarDecl || !(static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
				continue;
			}
			for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
				auto global = globals_.find(declarator.symbol);
				if (global != globals_.end() && !declarator.initializer && module_.globals[global->second].kind == Kind::Val) {
					uint32_t zero = zero_value(declarator.type, declarator.offset);
					if (zero != NONE) {
						emit(Op::SetGlobal, declarator.offset, global->second, zero);
					}
				}
			}
		}
		for (Node *member : p_class->members) {
			if (member->kind != NodeKind::VarDecl || !(static_cast<VarDecl *>(member)->modifiers.bits & Modifiers::Static)) {
				continue;
			}
			for (Declarator &declarator : static_cast<VarDecl *>(member)->declarators) {
				auto global = globals_.find(declarator.symbol);
				if (global != globals_.end() && declarator.initializer) {
					emit(Op::Stmt, declarator.offset);
					emit(Op::SetGlobal, declarator.offset, global->second,
							expression(declarator.initializer, module_.globals[global->second].kind));
				}
			}
		}
		emit(Op::ReturnVoid, p_class->name_offset);
	}

	void lower_unit(Unit &p_unit) {
		auto lower = [&](const List<Node *> &p_definitions) {
			for (Node *definition : p_definitions) {
				if (definition->kind == NodeKind::ClassDecl) {
					ClassDecl *decl = static_cast<ClassDecl *>(definition);
					if (!decl->symbol || !classes_.count(decl->symbol)) {
						continue;
					}
					lower_constructor(p_unit, decl);
					for (Node *member : decl->members) {
						if (member->kind == NodeKind::FunctionDecl) {
							FunctionDecl *function = static_cast<FunctionDecl *>(member);
							if (function->symbol && function->symbol != decl->symbol->klass->constructor &&
									function->accessor == FunctionDecl::None) {
								lower_function(p_unit, decl->symbol, function);
							}
						}
					}
				} else if (definition->kind == NodeKind::FunctionDecl) {
					lower_function(p_unit, nullptr, static_cast<FunctionDecl *>(definition));
				} else if (definition->kind == NodeKind::VarDecl) {
					lower_package_variables(p_unit, static_cast<VarDecl *>(definition));
				}
			}
		};
		if (p_unit.tree->package) {
			lower(p_unit.tree->package->definitions);
		}
		lower(p_unit.tree->definitions);
	}

	void lower_package_variables(Unit &p_unit, VarDecl *p_decl) {
		for (Declarator &declarator : p_decl->declarators) {
			auto global = globals_.find(declarator.symbol);
			if (global == globals_.end() || !declarator.initializer) {
				continue;
			}
			Function function;
			function.name = std::string(declarator.name) + "$init";
			function.file = file(&p_unit);
			function.position = declarator.offset;
			function.is_static = true;
			module_.functions.push_back(std::move(function));
			uint32_t index = (uint32_t)module_.functions.size() - 1;
			module_.initializers.push_back(index);
			begin(index, p_unit, nullptr);
			emit(Op::SetGlobal, declarator.offset, global->second,
					expression(declarator.initializer, module_.globals[global->second].kind));
			emit(Op::ReturnVoid, declarator.offset);
		}
	}

	// Classes in an order where each comes after its base.
	void collect(Unit &p_unit, std::vector<std::pair<Unit *, ClassDecl *>> &r_classes) {
		auto each = [&](const List<Node *> &p_definitions) {
			for (Node *definition : p_definitions) {
				if (definition->kind == NodeKind::ClassDecl && static_cast<ClassDecl *>(definition)->symbol) {
					r_classes.push_back({ &p_unit, static_cast<ClassDecl *>(definition) });
				}
			}
		};
		if (p_unit.tree->package) {
			each(p_unit.tree->package->definitions);
		}
		each(p_unit.tree->definitions);
	}

public:
	Lowerer(Compilation &p_compilation, Module &p_module) :
			compilation_(p_compilation), module_(p_module) {
		int_ = compilation_.builtin("int");
		uint_ = compilation_.builtin("uint");
		int64_ = compilation_.builtin("Int64");
		number_ = compilation_.builtin("Number");
		boolean_ = compilation_.builtin("Boolean");
		string_ = compilation_.builtin("String");
		object_ = compilation_.builtin("Object");
		error_ = compilation_.builtin("Error");
		if (Package *godot = compilation_.find_package("godot")) {
			auto found = godot->definitions.find("GodotObject");
			godot_object_ = found == godot->definitions.end() ? nullptr : found->second;
			found = godot->definitions.find("Signal");
			signal_ = found == godot->definitions.end() ? nullptr : found->second;
		}
		function_ = compilation_.builtin("Function");
	}

	bool run(LowerMode p_mode) {
		std::vector<Unit *> units;
		for (Unit &unit : compilation_.units()) {
			if (!unit.is_declaration && unit.tree) {
				units.push_back(&unit);
			}
		}
		if (units.empty()) {
			return false;
		}
		// The interpreter raises these itself, whether or not the program names them.
		class_index(error_, &module_.error_class);
		if (Symbol *type_error = compilation_.builtin("TypeError")) {
			class_index(type_error, &module_.type_error_class);
		}
		if (Symbol *range_error = compilation_.builtin("RangeError")) {
			class_index(range_error, &module_.range_error_class);
		}
		// Classes first, each after its base, so that field slots and method tables can be inherited.
		std::vector<std::pair<Unit *, ClassDecl *>> classes;
		for (Unit *unit : units) {
			collect(*unit, classes);
		}
		std::unordered_map<const Symbol *, bool> declared;
		std::vector<std::pair<Unit *, ClassDecl *>> ordered; // Each class after its base.
		for (size_t done = 0; done < classes.size();) {
			size_t before = done;
			for (auto &entry : classes) {
				Symbol *symbol = entry.second->symbol;
				Symbol *base = symbol->klass ? symbol->klass->base : nullptr;
				bool base_is_ours = false;
				for (auto &other : classes) {
					base_is_ours = base_is_ours || other.second->symbol == base;
				}
				if (!declared[symbol] && (!base_is_ours || declared[base])) {
					declared[symbol] = true;
					ordered.push_back(entry);
					done++;
				}
			}
			if (done == before) {
				break; // A cycle: the checker has reported it.
			}
		}
		for (auto &entry : ordered) {
			reserve_class(entry.second);
		}
		for (auto &entry : ordered) {
			declare_class(*entry.first, entry.second);
		}
		// Then everything else at package level.
		for (Unit *unit : units) {
			unit_ = unit;
			auto others = [&](const List<Node *> &p_definitions) {
				List<Node *> rest;
				std::vector<Node *> kept;
				for (Node *definition : p_definitions) {
					if (definition->kind != NodeKind::ClassDecl) {
						kept.push_back(definition);
					}
				}
				rest.items = kept.data();
				rest.count = (uint32_t)kept.size();
				for (Node *definition : rest) {
					switch (definition->kind) {
						case NodeKind::InterfaceDecl:
							refuse(static_cast<InterfaceDecl *>(definition)->name_offset, "an interface");
							break;
						case NodeKind::FunctionDecl: {
							FunctionDecl *decl = static_cast<FunctionDecl *>(definition);
							if (decl->symbol) {
								Function function;
								function.name = decl->symbol->package && !decl->symbol->package->name.empty() && !decl->symbol->file_private
										? decl->symbol->package->name + "." + std::string(decl->name)
										: std::string(decl->name);
								function.file = file(unit);
								function.position = decl->name_offset;
								function.is_static = true;
								functions_[decl->symbol] = (uint32_t)module_.functions.size();
								module_.functions.push_back(std::move(function));
							}
							break;
						}
						case NodeKind::VarDecl:
							for (Declarator &declarator : static_cast<VarDecl *>(definition)->declarators) {
								if (declarator.symbol) {
									globals_[declarator.symbol] = (uint32_t)module_.globals.size();
									module_.globals.push_back(
											{ std::string(declarator.name), written_kind(declarator.type, declarator.offset, Kind::F64) });
								}
							}
							break;
						default:
							break;
					}
				}
			};
			if (unit->tree->package) {
				others(unit->tree->package->definitions);
			}
			others(unit->tree->definitions);
		}
		// Static fields get their values a class at a time, a base class before its subclasses.
		for (auto &entry : ordered) {
			lower_statics(*entry.first, entry.second);
		}
		for (Unit *unit : units) {
			lower_unit(*unit);
		}
		// The program starts with the first class of the first file's package block.
		unit_ = units[0];
		if (p_mode == LowerMode::Library) {
			return !failed_;
		}
		if (units[0]->tree->package) {
			for (Node *definition : units[0]->tree->package->definitions) {
				if (definition->kind == NodeKind::ClassDecl && module_.entry_class == NONE) {
					auto found = classes_.find(static_cast<ClassDecl *>(definition)->symbol);
					if (found != classes_.end()) {
						module_.entry_class = found->second;
						Symbol *constructor = static_cast<ClassDecl *>(definition)->symbol->klass->constructor;
						FunctionDecl *decl = constructor && constructor->node ? static_cast<FunctionDecl *>(constructor->node) : nullptr;
						for (uint32_t i = 0; decl && i < decl->parameters.count; i++) {
							if (!decl->parameters[i].default_value) {
								failed_ = true;
								unit_->diagnostics.error(decl->name_offset,
										"the program's first class is created with no arguments, so its constructor can't require any");
								break;
							}
						}
					}
				}
			}
		}
		if (module_.entry_class == NONE && !failed_) {
			failed_ = true;
			units[0]->diagnostics.error(0, "the first file has no class to start the program with");
		}
		return !failed_;
	}
};

} // namespace

bool lower(Compilation &p_compilation, Module *r_module, LowerMode p_mode) {
	Lowerer lowerer(p_compilation, *r_module);
	return lowerer.run(p_mode);
}

} // namespace ir
} // namespace kas3
