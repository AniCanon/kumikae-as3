// The program model and the declare pass.
#include "names.h"

#include "ast_walk.h"

#include "builtins.h"
#include "parser.h"

namespace kas3 {

void resolve_bodies(Compilation &p_compilation); // names_bodies.cpp
void type_bodies(Compilation &p_compilation); // names_types.cpp
void check_class_rules(Compilation &p_compilation); // names_rules.cpp

const char *symbol_kind_name(SymbolKind p_kind) {
	switch (p_kind) {
		case SymbolKind::Class:
			return "class";
		case SymbolKind::Interface:
			return "interface";
		case SymbolKind::Function:
			return "function";
		case SymbolKind::Variable:
			return "variable";
		case SymbolKind::Field:
			return "field";
		case SymbolKind::Method:
			return "method";
		case SymbolKind::Property:
			return "property";
		case SymbolKind::Parameter:
			return "parameter";
		case SymbolKind::Local:
			return "local";
		case SymbolKind::LocalFunction:
			return "local function";
		case SymbolKind::CatchVariable:
			return "catch variable";
		case SymbolKind::Arguments:
			return "arguments";
	}
	return "?";
}

std::string dotted_name(const List<std::string_view> &p_parts, uint32_t p_count) {
	std::string result;
	for (uint32_t i = 0; i < p_count && i < p_parts.count; i++) {
		if (i) {
			result += '.';
		}
		result += p_parts[i];
	}
	return result;
}

// "Player" for "src/game/Player.as".
static std::string_view file_stem(const std::string &p_path) {
	std::string_view stem = p_path;
	size_t slash = stem.find_last_of("/\\");
	if (slash != std::string_view::npos) {
		stem.remove_prefix(slash + 1);
	}
	if (stem.size() > 3 && stem.substr(stem.size() - 3) == ".as") {
		stem.remove_suffix(3);
	}
	return stem;
}

static bool definition_name(Node *p_node, std::string_view *r_name, uint32_t *r_offset) {
	switch (p_node->kind) {
		case NodeKind::ClassDecl:
			*r_name = static_cast<ClassDecl *>(p_node)->name;
			*r_offset = static_cast<ClassDecl *>(p_node)->name_offset;
			return true;
		case NodeKind::InterfaceDecl:
			*r_name = static_cast<InterfaceDecl *>(p_node)->name;
			*r_offset = static_cast<InterfaceDecl *>(p_node)->name_offset;
			return true;
		case NodeKind::FunctionDecl:
			*r_name = static_cast<FunctionDecl *>(p_node)->name;
			*r_offset = static_cast<FunctionDecl *>(p_node)->name_offset;
			return true;
		case NodeKind::VarDecl: {
			VarDecl *decl = static_cast<VarDecl *>(p_node);
			if (decl->declarators.count == 0) {
				return false;
			}
			*r_name = decl->declarators[0].name;
			*r_offset = decl->declarators[0].offset;
			return true;
		}
		default:
			return false;
	}
}

std::shared_ptr<Parsed> parse_file(std::string p_path, std::string p_text) {
	std::shared_ptr<Parsed> parsed = std::make_shared<Parsed>(std::move(p_path), std::move(p_text));
	Parser parser(parsed->source, parsed->arena, parsed->diagnostics, &parsed->text_regions);
	parsed->tree = parser.parse_program();
	return parsed;
}

std::vector<std::shared_ptr<Parsed>> parse_builtins() {
	std::vector<std::shared_ptr<Parsed>> files;
	for (int i = 0; i < BUILTIN_FILE_COUNT; i++) {
		files.push_back(parse_file(BUILTIN_FILES[i].path, BUILTIN_FILES[i].text));
	}
	return files;
}

Compilation::Compilation() :
		Compilation(parse_builtins()) {}

Compilation::Compilation(const std::vector<std::shared_ptr<Parsed>> &p_builtins) {
	void_.kind = Type::Void;
	null_.kind = Type::Null;
	unknown_.kind = Type::Unknown;
	top_level_ = package("");
	for (const std::shared_ptr<Parsed> &file : p_builtins) {
		add(file, true)->is_builtin = true;
	}
}

Unit *Compilation::add(std::string p_path, std::string p_text, bool p_is_declaration) {
	return add(parse_file(std::move(p_path), std::move(p_text)), p_is_declaration);
}

Unit *Compilation::add(std::shared_ptr<Parsed> p_parsed, bool p_is_declaration) {
	if (p_parsed->used) {
		clear_annotations(p_parsed->tree);
	}
	p_parsed->used = true;
	units_.emplace_back(std::move(p_parsed));
	Unit &unit = units_.back();
	unit.is_declaration = p_is_declaration;
	return &unit;
}

void Compilation::check(bool p_through_syntax_errors) {
	if (checked_) {
		return;
	}
	checked_ = true;
	bool syntax_errors = false;
	for (Unit &unit : units_) {
		unit.broken = unit.diagnostics.has_errors();
		syntax_errors = syntax_errors || unit.broken;
	}
	if (syntax_errors) {
		if (!p_through_syntax_errors) {
			return;
		}
		// Nothing the passes find in a file that did not parse is certain, so nothing they find there is
		// reported. What they find in the other files is sorted out afterwards.
		for (Unit &unit : units_) {
			if (unit.broken) {
				unit.diagnostics.mute();
			}
		}
	}
	for (Unit &unit : units_) {
		declare_unit(unit);
	}
	auto object = top_level_->definitions.find("Object");
	object_ = object == top_level_->definitions.end() ? nullptr : object->second;
	auto vector = top_level_->definitions.find("Vector");
	vector_ = vector == top_level_->definitions.end() ? nullptr : vector->second;
	for (Unit &unit : units_) {
		link_imports(unit);
	}
	link_hierarchy();
	resolve_bodies(*this);
	check_class_rules(*this);
	type_bodies(*this);
	if (syntax_errors) {
		drop_doubtful_errors();
	}
	reusable_bodies_ = !has_errors();
	initial_type_count_ = types_.size();
}

bool Compilation::has_errors() const {
	for (const Unit &unit : units_) {
		if (unit.diagnostics.has_errors()) {
			return true;
		}
	}
	return false;
}

std::string Compilation::format_diagnostics(bool p_warnings) const {
	std::string result;
	for (const Unit &unit : units_) {
		result += unit.diagnostics.format(unit.source, p_warnings);
	}
	return result;
}

Symbol *Compilation::new_symbol(SymbolKind p_kind, std::string_view p_name, Unit *p_unit, uint32_t p_offset) {
	const bool body = p_kind == SymbolKind::Parameter || p_kind == SymbolKind::Local ||
			p_kind == SymbolKind::LocalFunction || p_kind == SymbolKind::CatchVariable || p_kind == SymbolKind::Arguments;
	auto &storage = body && p_unit ? p_unit->body_symbols : symbols_;
	storage.emplace_back();
	Symbol *symbol = &storage.back();
	symbol->kind = p_kind;
	symbol->name = p_name;
	symbol->unit = p_unit;
	symbol->offset = p_offset;
	return symbol;
}

Package *Compilation::package(const std::string &p_name) {
	Package &result = packages_[p_name];
	result.name = p_name;
	return &result;
}

Package *Compilation::find_package(const std::string &p_name) {
	auto found = packages_.find(p_name);
	return found == packages_.end() ? nullptr : &found->second;
}

std::string Compilation::qualified_name(const Symbol *p_symbol) const {
	std::string result;
	if (p_symbol->owner) {
		result += p_symbol->owner->name;
		result += '.';
		result += p_symbol->name;
	} else if (p_symbol->file_private) {
		result += p_symbol->name;
		result += " in this file";
	} else if (p_symbol->package && !p_symbol->package->name.empty()) {
		result += p_symbol->package->name;
		result += '.';
		result += p_symbol->name;
	} else {
		result += p_symbol->name;
	}
	return result;
}

std::string Compilation::where(const Unit &p_from, const Symbol *p_symbol) const {
	if (!p_symbol->unit || p_symbol->unit->is_builtin) {
		return "in the built-in library";
	}
	if (p_symbol->unit->source.path() == GODOT_DECLARATIONS_PATH) {
		return "in Godot's API"; // Not a file the reader can open.
	}
	Position position = p_symbol->unit->source.position(p_symbol->offset);
	if (p_symbol->unit == &p_from) {
		return "at line " + std::to_string(position.line);
	}
	return "at " + p_symbol->unit->source.path() + ":" + std::to_string(position.line) + ":" +
			std::to_string(position.column);
}

void Compilation::declare_unit(Unit &p_unit) {
	Program *tree = p_unit.tree;
	p_unit.package = package(tree->package ? dotted_name(tree->package->name, tree->package->name.count) : std::string());
	if (tree->package) {
		uint32_t count = 0;
		for (Node *definition : tree->package->definitions) {
			std::string_view name;
			uint32_t offset = 0;
			if (!p_unit.is_declaration && definition_name(definition, &name, &offset)) {
				if (count == 0) {
					if (name != file_stem(p_unit.source.path())) {
						p_unit.diagnostics.error(offset, quoted(name) + " must be defined in a file named " +
																 std::string(name) + ".as");
					}
				} else {
					p_unit.diagnostics.error(offset, "a file can define only one class, interface, function or "
													 "variable inside its package block");
				}
			}
			count++;
			declare_definition(p_unit, definition, true);
		}
	}
	for (Node *definition : tree->definitions) {
		declare_definition(p_unit, definition, false);
	}
}

void Compilation::check_native(Unit &p_unit, uint16_t p_modifiers, uint32_t p_offset) {
	if ((p_modifiers & Modifiers::Native) && !p_unit.is_declaration) {
		p_unit.diagnostics.error(p_offset, "'native' is only allowed in declaration files");
	}
}

// Called for everything that is not an instance method.
static void check_override_allowed(Unit &p_unit, uint16_t p_modifiers, uint32_t p_offset) {
	if (p_modifiers & Modifiers::Override) {
		p_unit.diagnostics.error(p_offset, "'override' can only be used on an instance method");
	}
}

void Compilation::declare_class(Unit &p_unit, ClassDecl *p_decl, bool p_in_block) {
	Symbol *symbol = new_symbol(SymbolKind::Class, p_decl->name, &p_unit, p_decl->name_offset);
	symbol->node = p_decl;
	symbol->modifiers = p_decl->modifiers.bits;
	classes_.emplace_back();
	symbol->klass = &classes_.back();
	symbol->klass->symbol = symbol;
	p_decl->symbol = symbol;
	if ((p_decl->modifiers.bits & Modifiers::Dynamic) && !p_unit.is_declaration) {
		p_unit.diagnostics.error(p_decl->name_offset, "dynamic classes are not supported");
	}
	if (p_unit.is_builtin) {
		for (const Metadata &tag : p_decl->metadata) {
			symbol->klass->incomplete |= tag.name == "Incomplete";
		}
	}
	check_override_allowed(p_unit, p_decl->modifiers.bits, p_decl->name_offset);
	check_native(p_unit, p_decl->modifiers.bits, p_decl->name_offset);
	declare_package_level(p_unit, symbol, p_in_block);
	declare_class_members(p_unit, symbol, p_decl);
}

void Compilation::declare_definition(Unit &p_unit, Node *p_node, bool p_in_block) {
	switch (p_node->kind) {
		case NodeKind::ClassDecl:
			declare_class(p_unit, static_cast<ClassDecl *>(p_node), p_in_block);
			break;
		case NodeKind::InterfaceDecl: {
			InterfaceDecl *decl = static_cast<InterfaceDecl *>(p_node);
			Symbol *symbol = new_symbol(SymbolKind::Interface, decl->name, &p_unit, decl->name_offset);
			symbol->node = decl;
			symbol->modifiers = decl->modifiers.bits;
			classes_.emplace_back();
			symbol->klass = &classes_.back();
			symbol->klass->symbol = symbol;
			decl->symbol = symbol;
			declare_package_level(p_unit, symbol, p_in_block);
			declare_interface_members(p_unit, symbol, decl);
		} break;
		case NodeKind::FunctionDecl: {
			FunctionDecl *decl = static_cast<FunctionDecl *>(p_node);
			Symbol *symbol = new_symbol(SymbolKind::Function, decl->name, &p_unit, decl->name_offset);
			symbol->node = decl;
			symbol->modifiers = decl->modifiers.bits;
			symbol->type = decl->return_type;
			decl->symbol = symbol;
			check_native(p_unit, decl->modifiers.bits, decl->name_offset);
			check_override_allowed(p_unit, decl->modifiers.bits, decl->name_offset);
			declare_package_level(p_unit, symbol, p_in_block);
		} break;
		case NodeKind::VarDecl: {
			VarDecl *decl = static_cast<VarDecl *>(p_node);
			for (Declarator &declarator : decl->declarators) {
				Symbol *symbol = new_symbol(SymbolKind::Variable, declarator.name, &p_unit, declarator.offset);
				symbol->node = decl;
				symbol->modifiers = decl->modifiers.bits;
				symbol->type = declarator.type;
				symbol->is_const = decl->is_const;
				declarator.symbol = symbol;
				check_override_allowed(p_unit, decl->modifiers.bits, declarator.offset);
				declare_package_level(p_unit, symbol, p_in_block);
			}
		} break;
		default:
			break;
	}
}

void Compilation::declare_package_level(Unit &p_unit, Symbol *p_symbol, bool p_in_block) {
	p_symbol->package = p_unit.package;
	p_symbol->file_private = !p_in_block;
	auto &table = p_in_block ? p_unit.package->definitions : p_unit.helpers;
	auto inserted = table.emplace(p_symbol->name, p_symbol);
	if (!inserted.second) {
		std::string message = quoted(p_symbol->name) + " is already defined " + where(p_unit, inserted.first->second);
		if (inserted.first->second->unit && inserted.first->second->unit->broken) {
			// The other one is in a file that did not parse: it may not be what that file means to define.
			p_unit.diagnostics.unresolved(p_symbol->offset, std::move(message));
		} else {
			p_unit.diagnostics.error(p_symbol->offset, std::move(message));
		}
	}
}

void Compilation::declare_member(Unit &p_unit, Symbol *p_owner, Symbol *p_member) {
	p_member->owner = p_owner;
	auto inserted = p_owner->klass->table(p_member->is_static()).emplace(p_member->name, p_member);
	if (!inserted.second) {
		p_unit.diagnostics.error(p_member->offset,
				quoted(p_member->name) + " is already defined " + where(p_unit, inserted.first->second));
	}
}

void Compilation::declare_accessor(Unit &p_unit, Symbol *p_owner, FunctionDecl *p_function) {
	bool getter = p_function->accessor == FunctionDecl::Getter;
	auto &members = p_owner->klass->table((p_function->modifiers.bits & Modifiers::Static) != 0);
	auto found = members.find(p_function->name);
	Symbol *property = nullptr;
	if (found == members.end()) {
		property = new_symbol(SymbolKind::Property, p_function->name, &p_unit, p_function->name_offset);
		property->modifiers = p_function->modifiers.bits;
		property->owner = p_owner;
		members.emplace(property->name, property);
	} else {
		property = found->second;
		if (property->kind != SymbolKind::Property || (getter ? property->getter : property->setter)) {
			p_unit.diagnostics.error(p_function->name_offset,
					quoted(p_function->name) + " is already defined " + where(p_unit, property));
			return;
		}
	}
	(getter ? property->getter : property->setter) = p_function;
	p_function->symbol = property;
}

void Compilation::declare_class_members(Unit &p_unit, Symbol *p_class, ClassDecl *p_decl) {
	for (Node *member : p_decl->members) {
		if (member->kind == NodeKind::VarDecl) {
			VarDecl *decl = static_cast<VarDecl *>(member);
			for (Declarator &declarator : decl->declarators) {
				Symbol *symbol = new_symbol(SymbolKind::Field, declarator.name, &p_unit, declarator.offset);
				symbol->node = decl;
				symbol->modifiers = decl->modifiers.bits;
				symbol->type = declarator.type;
				symbol->is_const = decl->is_const;
				declarator.symbol = symbol;
				check_override_allowed(p_unit, decl->modifiers.bits, declarator.offset);
				declare_member(p_unit, p_class, symbol);
			}
		} else if (member->kind == NodeKind::FunctionDecl) {
			FunctionDecl *decl = static_cast<FunctionDecl *>(member);
			check_native(p_unit, decl->modifiers.bits, decl->name_offset);
			if (decl->modifiers.bits & Modifiers::Static) {
				check_override_allowed(p_unit, decl->modifiers.bits, decl->name_offset);
			}
			if (decl->accessor != FunctionDecl::None) {
				declare_accessor(p_unit, p_class, decl);
				continue;
			}
			Symbol *symbol = new_symbol(SymbolKind::Method, decl->name, &p_unit, decl->name_offset);
			symbol->node = decl;
			symbol->modifiers = decl->modifiers.bits;
			symbol->type = decl->return_type;
			symbol->owner = p_class;
			decl->symbol = symbol;
			if (decl->name == p_decl->name) {
				if (p_class->klass->constructor) {
					p_unit.diagnostics.error(decl->name_offset, quoted(decl->name) + " is already defined " +
																		where(p_unit, p_class->klass->constructor));
				} else {
					p_class->klass->constructor = symbol;
				}
			} else {
				declare_member(p_unit, p_class, symbol);
			}
		}
	}
}

void Compilation::declare_interface_members(Unit &p_unit, Symbol *p_interface, InterfaceDecl *p_decl) {
	for (FunctionDecl *decl : p_decl->methods) {
		decl->modifiers.bits |= Modifiers::Public; // Interface members are always public.
		if (decl->accessor != FunctionDecl::None) {
			declare_accessor(p_unit, p_interface, decl);
			continue;
		}
		Symbol *symbol = new_symbol(SymbolKind::Method, decl->name, &p_unit, decl->name_offset);
		symbol->node = decl;
		symbol->modifiers = decl->modifiers.bits;
		symbol->type = decl->return_type;
		decl->symbol = symbol;
		declare_member(p_unit, p_interface, symbol);
	}
}

} // namespace kas3
