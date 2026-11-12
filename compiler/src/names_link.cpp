// The link pass: imports, package-level lookup, the class hierarchy, and the override rules.
#include "names.h"

namespace kas3 {

static std::string join(const List<std::string_view> &p_parts, uint32_t p_count) {
	std::string result;
	for (uint32_t i = 0; i < p_count; i++) {
		if (i) {
			result += '.';
		}
		result += p_parts[i];
	}
	return result;
}

static std::string quoted(std::string_view p_name) {
	std::string result = "'";
	result += p_name;
	result += '\'';
	return result;
}

// ---- lookup ----

Context Compilation::context_of(Unit &p_unit, bool p_in_block) {
	Context context;
	context.unit = &p_unit;
	context.package = p_unit.package;
	context.imports = p_in_block ? &p_unit.block_imports : &p_unit.outer_imports;
	return context;
}

Context Compilation::context_of(Symbol *p_class) {
	Context context = context_of(*p_class->unit, !p_class->file_private);
	context.imports = &p_class->klass->imports;
	context.klass = p_class;
	return context;
}

bool Compilation::visible(const Symbol *p_definition, const Context &p_from) const {
	if (p_definition->file_private) {
		return p_definition->unit == p_from.unit;
	}
	if (p_definition->modifiers & Modifiers::Public) {
		return true;
	}
	return p_definition->package == p_from.package;
}

static void add_match(const Compilation &p_compilation, const Context &p_from, Symbol *p_symbol, Found &r_found) {
	if (!p_symbol || p_symbol == r_found.first || p_symbol == r_found.second) {
		return;
	}
	if (!p_compilation.visible(p_symbol, p_from)) {
		if (!r_found.hidden) {
			r_found.hidden = p_symbol;
		}
	} else if (!r_found.first) {
		r_found.first = p_symbol;
	} else if (!r_found.second) {
		r_found.second = p_symbol;
	}
}

static Symbol *find_in(const std::unordered_map<std::string_view, Symbol *> &p_table, std::string_view p_name) {
	auto found = p_table.find(p_name);
	return found == p_table.end() ? nullptr : found->second;
}

// AS3 opens every namespace in reach at once: the file's helpers, the current package, the top-level package,
// and every import. Two different matches are an ambiguity, never a shadowing.
Found Compilation::lookup_package_level(const Context &p_from, std::string_view p_name) const {
	Found found;
	add_match(*this, p_from, find_in(p_from.unit->helpers, p_name), found);
	add_match(*this, p_from, find_in(p_from.package->definitions, p_name), found);
	add_match(*this, p_from, find_in(top_level_->definitions, p_name), found);
	for (const ImportScope *scope = p_from.imports; scope; scope = scope->parent) {
		for (const ResolvedImport &import : scope->imports) {
			if (import.symbol) {
				if (import.symbol->name == p_name) {
					add_match(*this, p_from, import.symbol, found);
				}
			} else {
				add_match(*this, p_from, find_in(import.package->definitions, p_name), found);
			}
		}
	}
	return found;
}

Found Compilation::lookup_qualified(const Context &p_from, const std::string &p_package,
		std::string_view p_name) const {
	Found found;
	auto package = packages_.find(p_package);
	if (package != packages_.end()) {
		add_match(*this, p_from, find_in(package->second.definitions, p_name), found);
	}
	return found;
}

std::string Compilation::ambiguous_message(std::string_view p_name, const Found &p_found) const {
	return quoted(p_name) + " is ambiguous: it could be " + qualified_name(p_found.first) + " or " +
			qualified_name(p_found.second);
}

Symbol *Compilation::resolve_type(const Context &p_from, TypeRef *p_type) {
	if (!p_type || p_type->form == TypeRef::Any || p_type->form == TypeRef::Void || p_type->name.count == 0) {
		return nullptr;
	}
	// Type arguments nest at most Parser::MAX_TYPE_DEPTH deep, so this recursion is shallow.
	if (p_type->form == TypeRef::Applied) {
		resolve_type(p_from, p_type->argument);
	}
	Diagnostics &diagnostics = p_from.unit->diagnostics;
	uint32_t last = p_type->name.count - 1;
	std::string shown = join(p_type->name, p_type->name.count);
	Found found = last == 0 ? lookup_package_level(p_from, p_type->name[0])
							: lookup_qualified(p_from, join(p_type->name, last), p_type->name[last]);
	if (!found.first) {
		if (found.hidden && last > 0) {
			diagnostics.error(p_type->offset, quoted(shown) + " is not public");
		} else {
			diagnostics.error(p_type->offset, "unknown type " + quoted(shown));
		}
		return nullptr;
	}
	if (found.second) {
		diagnostics.error(p_type->offset, ambiguous_message(shown, found));
		return nullptr;
	}
	if (!found.first->is_type()) {
		diagnostics.error(p_type->offset, quoted(shown) + " is not a type");
		return nullptr;
	}
	if (p_type->form == TypeRef::Applied && found.first != vector_) {
		diagnostics.error(p_type->offset, "only Vector takes a type parameter");
		return nullptr;
	}
	p_type->symbol = found.first;
	p_from.unit->references.push_back({ p_type->name_offsets[last], p_type->name[last], found.first });
	return found.first;
}

// ---- imports ----

void Compilation::resolve_imports(Unit &p_unit, const List<Import *> &p_imports, ImportScope &r_scope) {
	for (Import *import : p_imports) {
		if (import->name.count == 0) {
			continue;
		}
		uint32_t offset = import->name_offsets[0];
		if (import->wildcard) {
			std::string name = join(import->name, import->name.count);
			Package *found = find_package(name);
			if (!found || found->definitions.empty()) {
				p_unit.diagnostics.error(offset, "can't find package " + quoted(name));
				continue;
			}
			ResolvedImport resolved;
			resolved.package = found;
			r_scope.imports.push_back(resolved);
			continue;
		}
		uint32_t last = import->name.count - 1;
		std::string shown = join(import->name, import->name.count);
		Package *found = find_package(join(import->name, last));
		Symbol *symbol = found ? find_in(found->definitions, import->name[last]) : nullptr;
		if (!symbol) {
			p_unit.diagnostics.error(offset, "can't find " + quoted(shown));
			continue;
		}
		if (!(symbol->modifiers & Modifiers::Public) && symbol->package != p_unit.package) {
			p_unit.diagnostics.error(offset, quoted(shown) + " is not public");
			continue;
		}
		ResolvedImport resolved;
		resolved.symbol = symbol;
		r_scope.imports.push_back(resolved);
	}
}

static void collect_class_imports(const List<Node *> &p_members, std::vector<Import *> &r_imports) {
	for (Node *member : p_members) {
		if (member->kind == NodeKind::Import) {
			r_imports.push_back(static_cast<Import *>(member));
		}
	}
}

void Compilation::link_imports(Unit &p_unit) {
	Program *tree = p_unit.tree;
	resolve_imports(p_unit, tree->imports, p_unit.outer_imports);
	if (tree->package) {
		resolve_imports(p_unit, tree->package->imports, p_unit.block_imports);
	}
	auto link_definitions = [&](const List<Node *> &p_definitions, ImportScope &p_parent) {
		for (Node *definition : p_definitions) {
			if (definition->kind == NodeKind::ClassDecl) {
				ClassDecl *decl = static_cast<ClassDecl *>(definition);
				decl->symbol->klass->imports.parent = &p_parent;
				std::vector<Import *> imports;
				collect_class_imports(decl->members, imports);
				List<Import *> list;
				list.items = imports.data();
				list.count = (uint32_t)imports.size();
				resolve_imports(p_unit, list, decl->symbol->klass->imports);
			} else if (definition->kind == NodeKind::InterfaceDecl) {
				static_cast<InterfaceDecl *>(definition)->symbol->klass->imports.parent = &p_parent;
			}
		}
	};
	if (tree->package) {
		link_definitions(tree->package->definitions, p_unit.block_imports);
	}
	link_definitions(tree->definitions, p_unit.outer_imports);
}

// ---- hierarchy ----

void Compilation::link_class(Symbol *p_class) {
	ClassDecl *decl = static_cast<ClassDecl *>(p_class->node);
	ClassInfo *info = p_class->klass;
	Context context = context_of(p_class);
	Diagnostics &diagnostics = p_class->unit->diagnostics;
	if (decl->extends) {
		Symbol *base = resolve_type(context, decl->extends);
		if (!base) {
			info->broken = true;
		} else if (base->kind == SymbolKind::Interface) {
			diagnostics.error(decl->extends->offset, "a class can't extend the interface " + quoted(base->name) +
															 "; use 'implements'");
			info->broken = true;
		} else {
			if (base->modifiers & Modifiers::Final) {
				diagnostics.error(decl->extends->offset, "can't extend " + quoted(base->name) + ": it is final");
			}
			info->base = base;
		}
	} else if (p_class != object_) {
		info->base = object_;
	}
	for (TypeRef *type : decl->implements) {
		Symbol *interface = resolve_type(context, type);
		if (interface && interface->kind != SymbolKind::Interface) {
			diagnostics.error(type->offset, quoted(interface->name) + " is not an interface");
			interface = nullptr;
		}
		info->interfaces.push_back(interface);
	}
}

void Compilation::link_interface(Symbol *p_interface) {
	InterfaceDecl *decl = static_cast<InterfaceDecl *>(p_interface->node);
	Context context = context_of(p_interface);
	for (TypeRef *type : decl->extends) {
		Symbol *interface = resolve_type(context, type);
		if (interface && interface->kind != SymbolKind::Interface) {
			p_interface->unit->diagnostics.error(type->offset, quoted(interface->name) + " is not an interface");
			interface = nullptr;
		}
		// Kept parallel to decl->extends: a null marks one that did not resolve.
		p_interface->klass->interfaces.push_back(interface);
	}
}

// A class in a cycle loses its base, so that every walk up a hierarchy ends. One error per cycle, at the
// class declared first.
void Compilation::cut_class_cycles() {
	uint32_t mark = 0;
	for (ClassInfo &start : classes_) {
		if (start.symbol->kind != SymbolKind::Class) {
			continue;
		}
		mark++;
		for (ClassInfo *info = &start; info; info = info->base ? info->base->klass : nullptr) {
			if (info->mark == mark) {
				if (info == &start) {
					ClassDecl *decl = static_cast<ClassDecl *>(start.symbol->node);
					start.symbol->unit->diagnostics.error(decl->extends->offset,
							quoted(start.symbol->name) + " inherits from itself");
					start.base = nullptr;
					start.broken = true;
				}
				break;
			}
			info->mark = mark;
		}
	}
}

// The same for interfaces, which may extend several. Depth-first, with an explicit stack.
void Compilation::cut_interface_cycles() {
	enum : uint32_t { WHITE = 0, GREY = 1, BLACK = 2 };
	struct Frame {
		ClassInfo *info;
		size_t next;
	};
	for (ClassInfo &info : classes_) {
		if (info.symbol->kind == SymbolKind::Interface) {
			info.mark = WHITE;
		}
	}
	std::vector<Frame> stack;
	for (ClassInfo &start : classes_) {
		if (start.symbol->kind != SymbolKind::Interface || start.mark != WHITE) {
			continue;
		}
		start.mark = GREY;
		stack.push_back({ &start, 0 });
		while (!stack.empty()) {
			Frame &frame = stack.back();
			if (frame.next >= frame.info->interfaces.size()) {
				frame.info->mark = BLACK;
				stack.pop_back();
				continue;
			}
			Symbol *parent = frame.info->interfaces[frame.next++];
			if (!parent) {
				continue;
			}
			ClassInfo *target = parent->klass;
			if (target->mark == WHITE) {
				target->mark = GREY;
				stack.push_back({ target, 0 }); // Invalidates `frame`; it is not used again this round.
			} else if (target->mark == GREY) {
				// A cycle. Cut it at the interface that started it: the edge it is currently following.
				for (Frame &ancestor : stack) {
					if (ancestor.info == target) {
						size_t edge = ancestor.next - 1;
						InterfaceDecl *decl = static_cast<InterfaceDecl *>(target->symbol->node);
						target->symbol->unit->diagnostics.error(decl->extends[(uint32_t)edge]->offset,
								quoted(target->symbol->name) + " inherits from itself");
						target->interfaces[edge] = nullptr;
						target->broken = true;
						break;
					}
				}
			}
		}
	}
}

bool Compilation::hierarchy_broken(Symbol *p_class) const {
	for (const ClassInfo *info = p_class->klass; info; info = info->base ? info->base->klass : nullptr) {
		if (info->broken) {
			return true;
		}
	}
	return false;
}

// What code in a class of package p_package may name on a base class.
static bool inherited_visible(const Symbol *p_member, const Symbol *p_base, const Package *p_package) {
	if (p_member->modifiers & Modifiers::Private) {
		return false;
	}
	if (p_member->modifiers & (Modifiers::Public | Modifiers::Protected)) {
		return true;
	}
	return p_base->package == p_package;
}

Symbol *Compilation::find_member(Symbol *p_class, std::string_view p_name, Symbol **r_hidden) const {
	Symbol *own = find_in(p_class->klass->members, p_name);
	if (own) {
		return own;
	}
	for (Symbol *base = p_class->klass->base; base; base = base->klass->base) {
		Symbol *member = find_in(base->klass->members, p_name);
		if (!member) {
			continue;
		}
		if (inherited_visible(member, base, p_class->package)) {
			return member;
		}
		if (r_hidden && !*r_hidden) {
			*r_hidden = member;
		}
	}
	return nullptr;
}

void Compilation::check_overrides(Symbol *p_class) {
	Diagnostics &diagnostics = p_class->unit->diagnostics;
	// The instance member of that name that a base class hands down, if any.
	auto inherited = [&](std::string_view p_name) -> Symbol * {
		for (Symbol *base = p_class->klass->base; base; base = base->klass->base) {
			Symbol *member = find_in(base->klass->members, p_name);
			if (member && !member->is_static() && inherited_visible(member, base, p_class->package)) {
				return member;
			}
		}
		return nullptr;
	};
	// p_accessor: None for a plain method, Getter or Setter for half of a property.
	auto check_function = [&](FunctionDecl *p_function, Symbol *p_base) {
		std::string name = quoted(p_function->name);
		uint32_t offset = p_function->name_offset;
		FunctionDecl *overridden = nullptr;
		bool free_slot = false; // The base has a property of this name, but not this half of it.
		if (p_base) {
			if (p_function->accessor == FunctionDecl::None) {
				overridden = p_base->kind == SymbolKind::Method ? static_cast<FunctionDecl *>(p_base->node) : nullptr;
			} else if (p_base->kind == SymbolKind::Property) {
				overridden = p_function->accessor == FunctionDecl::Getter ? p_base->getter : p_base->setter;
				free_slot = overridden == nullptr;
			}
		}
		std::string base_name = p_base ? std::string(p_base->owner->name) : std::string();
		if (p_function->modifiers.bits & Modifiers::Override) {
			if (!p_base || free_slot) {
				diagnostics.error(offset, name + " is marked 'override' but no base class has a method to override");
			} else if (!overridden) {
				diagnostics.error(offset, "can't override " + name + ": it is not a method in " + base_name);
			} else if (overridden->modifiers.bits & Modifiers::Final) {
				diagnostics.error(offset, "can't override " + name + ": it is final in " + base_name);
			}
		} else if (overridden) {
			diagnostics.error(offset, name + " overrides a method of " + base_name + " and must be marked 'override'");
		} else if (p_base && !free_slot) {
			diagnostics.error(offset, name + " conflicts with " + name + " inherited from " + base_name);
		}
	};
	for (auto &entry : p_class->klass->members) {
		Symbol *member = entry.second;
		if (member->is_static()) {
			continue;
		}
		Symbol *base = inherited(member->name);
		switch (member->kind) {
			case SymbolKind::Field:
				if (base) {
					diagnostics.error(member->offset, quoted(member->name) + " conflicts with " + quoted(member->name) +
															  " inherited from " + std::string(base->owner->name));
				}
				break;
			case SymbolKind::Method:
				check_function(static_cast<FunctionDecl *>(member->node), base);
				break;
			case SymbolKind::Property:
				if (member->getter) {
					check_function(member->getter, base);
				}
				if (member->setter) {
					check_function(member->setter, base);
				}
				break;
			default:
				break;
		}
	}
}

void Compilation::link_hierarchy() {
	for (ClassInfo &info : classes_) {
		if (info.symbol->kind == SymbolKind::Class) {
			link_class(info.symbol);
		} else {
			link_interface(info.symbol);
		}
	}
	cut_class_cycles();
	cut_interface_cycles();
	for (ClassInfo &info : classes_) {
		if (info.symbol->kind == SymbolKind::Class && !hierarchy_broken(info.symbol)) {
			check_overrides(info.symbol);
		}
	}
}

} // namespace kas3
