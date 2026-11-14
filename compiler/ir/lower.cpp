#include "lowerer.h"

namespace kas3 {
namespace ir {

void Lowerer::refuse(uint32_t p_offset, const std::string &p_what) {
	failed_ = true;
	if (unit_) {
		unit_->diagnostics.error(p_offset, p_what + " is not supported by the interpreter yet");
	}
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
	for (size_t i = 0; i < module_.wides.size(); i++) {
		if (module_.wides[i] == p_value) {
			return (uint32_t)i;
		}
	}
	module_.wides.push_back(p_value);
	return (uint32_t)module_.wides.size() - 1;
}

uint32_t Lowerer::number(double p_value) {
	for (size_t i = 0; i < module_.numbers.size(); i++) {
		// Bit-for-bit: 0 and -0 are different constants, and NaN is itself.
		if (std::memcmp(&module_.numbers[i], &p_value, sizeof(double)) == 0) {
			return (uint32_t)i;
		}
	}
	module_.numbers.push_back(p_value);
	return (uint32_t)module_.numbers.size() - 1;
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

bool lower(Compilation &p_compilation, Module *r_module, LowerMode p_mode) {
	Lowerer lowerer(p_compilation, *r_module);
	return lowerer.run(p_mode);
}

} // namespace ir
} // namespace kas3
