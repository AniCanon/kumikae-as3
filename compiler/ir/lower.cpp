#include "lowerer.h"

namespace kas3 {
namespace ir {

void Lowerer::refuse(uint32_t p_offset, const std::string &p_what) {
	failed_ = true;
	// One refusal is enough for a statement: what follows in it most often follows from the first. And a
	// statement that uses a variable whose type was refused where it is declared is said nothing more of.
	if (!unit_ || (statement_ && (statement_ == refused_in_ || uses_refused_type(statement_)))) {
		return;
	}
	refused_in_ = statement_;
	unit_->diagnostics.error(p_offset, p_what + " is not supported by the interpreter yet");
}

bool Lowerer::uses_refused_type(const Stmt *p_statement) {
	if (refused_types_.empty()) {
		return false;
	}
	bool found = false;
	walk(const_cast<Stmt *>(p_statement), [&](Node *p_node) {
		if (p_node->kind == NodeKind::Name) {
			const Symbol *symbol = static_cast<Name *>(p_node)->symbol;
			found = found || (symbol && symbol->type && refused_types_.count(compilation_.type_of(symbol->type)));
		}
	});
	return found;
}

uint32_t Lowerer::string(const std::string &p_text) {
	auto found = strings_.find(p_text);
	if (found != strings_.end()) {
		return found->second;
	}
	module_.strings.push_back(p_text);
	return strings_[p_text] = (uint32_t)module_.strings.size() - 1;
}

uint32_t Lowerer::wide(int64_t p_value) {
	const auto [found, added] = wides_.try_emplace(p_value, (uint32_t)module_.wides.size());
	if (added) {
		module_.wides.push_back(p_value);
	}
	return found->second;
}

uint32_t Lowerer::number(double p_value) {
	// By its bits: 0 and -0 are different constants, and NaN is itself.
	uint64_t bits = 0;
	std::memcpy(&bits, &p_value, sizeof(bits));
	const auto [found, added] = numbers_.try_emplace(bits, (uint32_t)module_.numbers.size());
	if (added) {
		module_.numbers.push_back(p_value);
	}
	return found->second;
}

uint32_t Lowerer::file(const Unit *p_unit) {
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

void Lowerer::lower_unit(Unit &p_unit) {
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
						if (function->symbol && function->symbol != decl->symbol->klass->constructor) {
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

void Lowerer::collect(Unit &p_unit, std::vector<std::pair<Unit *, ClassDecl *>> &r_classes) {
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

Lowerer::Lowerer(Compilation &p_compilation, Module &p_module) :
		compilation_(p_compilation), module_(p_module) {
	int_ = compilation_.builtin("int");
	uint_ = compilation_.builtin("uint");
	int64_ = compilation_.builtin("Int64");
	number_ = compilation_.builtin("Number");
	math_ = compilation_.builtin("Math");
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
	array_ = compilation_.builtin("Array");
}

// The interpreter raises these itself, whether or not the program names them.
void Lowerer::find_error_classes() {
	class_index(error_, &module_.error_class);
	if (Symbol *type_error = compilation_.builtin("TypeError")) {
		class_index(type_error, &module_.type_error_class);
	}
	if (Symbol *range_error = compilation_.builtin("RangeError")) {
		class_index(range_error, &module_.range_error_class);
	}
}

// The program's classes, each after its base, so that field slots and method tables can be inherited.
// A class in a cycle is left out: the checker has reported it.
std::vector<Lowerer::ClassInUnit> Lowerer::classes_in_order(const std::vector<Unit *> &p_units) {
	std::vector<ClassInUnit> classes;
	for (Unit *unit : p_units) {
		collect(*unit, classes);
	}
	std::unordered_set<const Symbol *> ours;
	for (const ClassInUnit &entry : classes) {
		ours.insert(entry.second->symbol);
	}
	std::unordered_set<const Symbol *> declared;
	std::vector<ClassInUnit> ordered;
	for (size_t done = 0; done < classes.size();) {
		const size_t before = done;
		for (const ClassInUnit &entry : classes) {
			const Symbol *symbol = entry.second->symbol;
			const Symbol *base = symbol->klass ? symbol->klass->base : nullptr;
			if (!declared.count(symbol) && (!ours.count(base) || declared.count(base))) {
				declared.insert(symbol);
				ordered.push_back(entry);
				done++;
			}
		}
		if (done == before) {
			break;
		}
	}
	return ordered;
}

void Lowerer::declare_package_function(Unit *p_unit, FunctionDecl *p_decl) {
	if (!p_decl->symbol) {
		return;
	}
	const Symbol *symbol = p_decl->symbol;
	Function function;
	function.name = symbol->package && !symbol->package->name.empty() && !symbol->file_private
			? symbol->package->name + "." + std::string(p_decl->name)
			: std::string(p_decl->name);
	function.file = file(p_unit);
	function.position = p_decl->name_offset;
	function.is_static = true;
	functions_[p_decl] = (uint32_t)module_.functions.size();
	module_.functions.push_back(std::move(function));
}

// What a file holds at package level other than classes: functions and variables get their places, and
// an interface is refused.
void Lowerer::declare_package_level(Unit *p_unit, const List<Node *> &p_definitions) {
	for (Node *definition : p_definitions) {
		switch (definition->kind) {
			case NodeKind::InterfaceDecl:
				refuse(static_cast<InterfaceDecl *>(definition)->name_offset, "an interface");
				break;
			case NodeKind::FunctionDecl:
				declare_package_function(p_unit, static_cast<FunctionDecl *>(definition));
				break;
			case NodeKind::VarDecl:
				for (Declarator &declarator : static_cast<VarDecl *>(definition)->declarators) {
					if (declarator.symbol) {
						globals_[declarator.symbol] = (uint32_t)module_.globals.size();
						module_.globals.push_back({ std::string(declarator.name), written_kind(declarator.type, declarator.offset, Kind::F64) });
					}
				}
				break;
			default:
				break;
		}
	}
}

// The program starts with the first class of the first file's package block, made with no arguments.
void Lowerer::find_entry_class(Unit *p_first) {
	for (Node *definition : p_first->tree->package ? p_first->tree->package->definitions : List<Node *>()) {
		if (definition->kind != NodeKind::ClassDecl || module_.entry_class != NONE) {
			continue;
		}
		const Symbol *symbol = static_cast<ClassDecl *>(definition)->symbol;
		auto found = classes_.find(symbol);
		if (found == classes_.end()) {
			continue;
		}
		module_.entry_class = found->second;
		const Symbol *constructor = symbol->klass->constructor;
		const FunctionDecl *decl = constructor && constructor->node ? static_cast<const FunctionDecl *>(constructor->node) : nullptr;
		for (uint32_t i = 0; decl && i < decl->parameters.count; i++) {
			if (!decl->parameters[i].default_value) {
				failed_ = true;
				unit_->diagnostics.error(decl->name_offset,
						"the program's first class is created with no arguments, so its constructor can't require any");
				break;
			}
		}
	}
	if (module_.entry_class == NONE && !failed_) {
		failed_ = true;
		p_first->diagnostics.error(0, "the first file has no class to start the program with");
	}
}

bool Lowerer::run(LowerMode p_mode) {
	std::vector<Unit *> units;
	for (Unit &unit : compilation_.units()) {
		if (!unit.is_declaration && unit.tree) {
			units.push_back(&unit);
		}
	}
	if (units.empty()) {
		return false;
	}
	find_error_classes();
	const std::vector<ClassInUnit> classes = classes_in_order(units);
	for (const ClassInUnit &entry : classes) {
		reserve_class(entry.second);
	}
	for (const ClassInUnit &entry : classes) {
		declare_class(*entry.first, entry.second);
	}
	for (Unit *unit : units) {
		unit_ = unit;
		if (unit->tree->package) {
			declare_package_level(unit, unit->tree->package->definitions);
		}
		declare_package_level(unit, unit->tree->definitions);
	}
	// Static fields get their values a class at a time, a base class before its subclasses.
	for (const ClassInUnit &entry : classes) {
		lower_statics(*entry.first, entry.second);
	}
	for (Unit *unit : units) {
		lower_unit(*unit);
	}
	unit_ = units[0];
	if (p_mode != LowerMode::Library) {
		find_entry_class(units[0]);
	}
	return !failed_;
}

bool lower(Compilation &p_compilation, Module *r_module, LowerMode p_mode) {
	Lowerer lowerer(p_compilation, *r_module);
	return lowerer.run(p_mode);
}

} // namespace ir
} // namespace kas3
