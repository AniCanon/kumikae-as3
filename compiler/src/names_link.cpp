// The link pass: imports, the class hierarchy, and the override rules. Package-level lookup is in
// names_lookup.cpp, and what a class has from its bases in names_members.cpp.
#include "names.h"

namespace kas3 {

void Compilation::resolve_imports(Unit &p_unit, const List<Import *> &p_imports, ImportScope &r_scope) {
	for (Import *import : p_imports) {
		if (import->name.count == 0) {
			continue;
		}
		uint32_t offset = import->name_offsets[0];
		if (import->wildcard) {
			std::string name = dotted_name(import->name, import->name.count);
			Package *found = find_package(name);
			if (!found || found->definitions.empty()) {
				p_unit.diagnostics.unresolved(offset, "can't find package " + quoted(name));
				p_unit.failed_import = true;
				r_scope.failed_wildcard = true;
				continue;
			}
			ResolvedImport resolved;
			resolved.package = found;
			r_scope.imports.push_back(resolved);
			continue;
		}
		uint32_t last = import->name.count - 1;
		std::string shown = dotted_name(import->name, import->name.count);
		Package *found = find_package(dotted_name(import->name, last));
		Symbol *symbol = found ? find_in(found->definitions, import->name[last]) : nullptr;
		if (!symbol) {
			p_unit.diagnostics.unresolved(offset, "can't find " + quoted(shown));
			p_unit.failed_import = true;
			r_scope.failed.push_back(import->name[last]);
			continue;
		}
		if (!(symbol->modifiers & Modifiers::Public) && symbol->package != p_unit.package) {
			p_unit.diagnostics.unresolved(offset, quoted(shown) + " is not public");
			p_unit.failed_import = true;
			r_scope.failed.push_back(import->name[last]);
			continue;
		}
		import->symbol = symbol;
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

// A class that declares one half of a property beside an inherited other half sees both. Bases first.
void Compilation::inherit_properties(Symbol *p_class) {
	std::vector<Symbol *> chain;
	for (Symbol *owner = p_class; owner && !owner->klass->inherited; owner = owner->klass->base) {
		chain.push_back(owner);
	}
	for (size_t i = chain.size(); i-- > 0;) {
		Symbol *owner = chain[i];
		owner->klass->inherited = true;
		if (!owner->klass->base) {
			continue;
		}
		for (auto &entry : owner->klass->members) {
			Symbol *property = entry.second;
			if (property->kind != SymbolKind::Property || property->is_static() ||
					(property->getter && property->setter)) {
				continue;
			}
			Symbol *base = find_member(owner->klass->base, property->name, false, nullptr, nullptr);
			if (base && base->kind == SymbolKind::Property && !base->is_static() &&
					inherited_visible(base, base->owner, owner->package)) {
				if (!property->getter) {
					property->getter = base->getter;
				}
				if (!property->setter) {
					property->setter = base->setter;
				}
			}
		}
	}
}

namespace {

// The instance member of that name that a base class of p_class hands down, if any.
Symbol *inherited_member(Symbol *p_class, std::string_view p_name) {
	for (Symbol *base = p_class->klass->base; base; base = base->klass->base) {
		Symbol *member = find_in(base->klass->members, p_name);
		if (member && !member->is_static() && inherited_visible(member, base, p_class->package)) {
			return member;
		}
	}
	return nullptr;
}

// True if a built-in base declares the name at all, visible or not, static or not.
bool builtin_base_has(Symbol *p_class, std::string_view p_name) {
	for (Symbol *base = p_class->klass->base; base; base = base->klass->base) {
		if (base->unit->is_builtin &&
				(find_in(base->klass->members, p_name) || find_in(base->klass->statics, p_name))) {
			return true;
		}
	}
	return false;
}

// The override rules for one method or one half of a property, against what a base hands down (p_base).
void check_override(Diagnostics &p_diagnostics, Symbol *p_class, FunctionDecl *p_function, Symbol *p_base) {
	// A built-in member may be redefined with or without `override`. Which one AS3 wants depends on
	// whether the member is in the AS3 namespace, on the prototype, or public; that is not modelled.
	if (p_base && p_base->unit->is_builtin) {
		return;
	}
	if (!p_base && (p_function->modifiers.bits & Modifiers::Override) && builtin_base_has(p_class, p_function->name)) {
		return;
	}
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
			p_diagnostics.error(offset, name + " is marked 'override' but no base class has a method to override");
		} else if (!overridden) {
			p_diagnostics.error(offset, "can't override " + name + ": it is not a method in " + base_name);
		} else if (overridden->modifiers.bits & Modifiers::Final) {
			p_diagnostics.error(offset, "can't override " + name + ": it is final in " + base_name);
		}
	} else if (overridden) {
		p_diagnostics.error(offset, name + " overrides a method of " + base_name + " and must be marked 'override'");
	} else if (p_base && !free_slot) {
		p_diagnostics.error(offset, name + " conflicts with " + name + " inherited from " + base_name);
	}
}

} // namespace

void Compilation::check_overrides(Symbol *p_class) {
	Diagnostics &diagnostics = p_class->unit->diagnostics;
	// In the order written, so that the errors kept when there are too many are the first in the file.
	for (Node *node : static_cast<ClassDecl *>(p_class->node)->members) {
		if (node->kind == NodeKind::VarDecl) {
			VarDecl *decl = static_cast<VarDecl *>(node);
			if (decl->modifiers.bits & Modifiers::Static) {
				continue;
			}
			for (Declarator &declarator : decl->declarators) {
				Symbol *base = inherited_member(p_class, declarator.name);
				if (base && !base->unit->is_builtin) {
					diagnostics.error(declarator.offset, quoted(declarator.name) + " conflicts with " +
																 quoted(declarator.name) + " inherited from " +
																 std::string(base->owner->name));
				}
			}
		} else if (node->kind == NodeKind::FunctionDecl) {
			FunctionDecl *function = static_cast<FunctionDecl *>(node);
			if ((function->modifiers.bits & Modifiers::Static) || function->symbol == p_class->klass->constructor) {
				continue;
			}
			check_override(diagnostics, p_class, function, inherited_member(p_class, function->name));
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
	settle_hierarchy();
	for (ClassInfo &info : classes_) {
		if (info.symbol->kind == SymbolKind::Class && !hierarchy_broken(info.symbol)) {
			check_overrides(info.symbol);
			inherit_properties(info.symbol);
		}
	}
}

} // namespace kas3
