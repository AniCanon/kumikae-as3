// What a file's check read from other files, worked out from what the check wrote onto its tree: the
// symbols its names found, the types its expressions have, and the names it looked up whether or not they
// found anything. Declaration files never change in place, so nothing of theirs is a dependency.
#include "dependencies.h"
#include "names.h"

#include "ast_walk.h"

#include <algorithm>
#include <functional>

namespace kas3 {

namespace {

bool in_program(const Symbol *p_symbol) {
	return p_symbol && p_symbol->unit && !p_symbol->unit->is_declaration;
}

bool local(const Symbol *p_symbol) {
	switch (p_symbol->kind) {
		case SymbolKind::Parameter:
		case SymbolKind::Local:
		case SymbolKind::LocalFunction:
		case SymbolKind::CatchVariable:
		case SymbolKind::Arguments:
			return true;
		default:
			return false;
	}
}

class Collector {
	const Unit &unit_;
	std::vector<Dependency> &keys_;
	std::vector<const Symbol *> chain_;

public:
	Collector(const Unit &p_unit, std::vector<Dependency> &r_keys) :
			unit_(p_unit), keys_(r_keys) {}

	void symbol(const Symbol *p_symbol) {
		if (in_program(p_symbol) && !local(p_symbol)) {
			keys_.push_back(on_symbol(p_symbol));
			if (p_symbol->klass) {
				keys_.push_back(on_chain(p_symbol));
			}
		}
	}

	void type(const Type *p_type) {
		for (; p_type; p_type = p_type->element) {
			if (in_program(p_type->symbol) && p_type->symbol->klass) {
				keys_.push_back(on_chain(p_type->symbol));
			}
		}
	}

	void type_ref(const TypeRef *p_type) {
		if (p_type && p_type->name.count) {
			keys_.push_back(on_name(p_type->name[p_type->name.count - 1]));
			symbol(p_type->symbol);
		}
	}

	void import(const Import *p_import) {
		if (p_import->name.count == 0) {
			return;
		}
		if (p_import->wildcard) {
			keys_.push_back(on_package(dotted_name(p_import->name, p_import->name.count)));
		} else {
			keys_.push_back(on_name(p_import->name[p_import->name.count - 1]));
			symbol(p_import->symbol);
		}
	}

	void imports(const List<Import *> &p_imports) {
		for (const Import *import : p_imports) {
			this->import(import);
		}
	}

	// A member looked up by name through a class: whatever the class or one of its supertypes in the
	// program declares under that name, now or after an edit, is what was found.
	void members(const Symbol *p_class, std::string_view p_name) {
		if (!p_class || !p_class->klass) {
			return;
		}
		chain_.assign(1, p_class);
		for (size_t i = 0; i < chain_.size(); ++i) {
			const ClassInfo *info = chain_[i]->klass;
			if (in_program(chain_[i])) {
				keys_.push_back(on_member(chain_[i], p_name));
			}
			for (const Symbol *next : info->interfaces) {
				if (next && next->klass && std::find(chain_.begin(), chain_.end(), next) == chain_.end()) {
					chain_.push_back(next);
				}
			}
			if (info->base && info->base->klass && std::find(chain_.begin(), chain_.end(), info->base) == chain_.end()) {
				chain_.push_back(info->base);
			}
		}
	}

	void cited() {
		for (const Unit *other : unit_.cited) {
			keys_.push_back(on_positions(other));
		}
	}

	void node(Node *p_node, const Symbol *p_class) {
		switch (p_node->kind) {
			case NodeKind::Name: {
				const Name *name = static_cast<const Name *>(p_node);
				if (name->symbol && local(name->symbol)) {
					break;
				}
				keys_.push_back(on_name(name->text));
				members(p_class, name->text);
				symbol(name->symbol);
				if (in_program(name->symbol) && name->symbol->klass) {
					keys_.push_back(on_member(name->symbol, std::string_view())); // `new C()`: its constructor.
				}
			} break;
			case NodeKind::Member: {
				const Member *member = static_cast<const Member *>(p_node);
				const Type *object = member->object ? member->object->type : nullptr;
				if (object && (object->kind == Type::Instance || object->kind == Type::ClassObject)) {
					members(object->kind == Type::Instance ? object->symbol : object->element ? object->element->symbol : nullptr,
							member->name);
				} else {
					keys_.push_back(on_name(member->name)); // A package path, or a value of no known type.
				}
				const Symbol *found = member->resolved();
				symbol(found);
				if (in_program(found) && found->klass) {
					keys_.push_back(on_member(found, std::string_view()));
				}
			} break;
			case NodeKind::TypeRef:
				type_ref(static_cast<const TypeRef *>(p_node));
				break;
			case NodeKind::Import:
				import(static_cast<const Import *>(p_node));
				break;
			default:
				break;
		}
		if (is_expression(p_node->kind)) {
			type(static_cast<const Expr *>(p_node)->type);
		}
	}
};

void each_definition(const Unit &p_unit, const std::function<void(Node *)> &p_visit) {
	if (p_unit.tree->package) {
		for (Node *node : p_unit.tree->package->definitions) {
			p_visit(node);
		}
	}
	for (Node *node : p_unit.tree->definitions) {
		p_visit(node);
	}
}

} // namespace

// What the declarations read: imports, supertypes (whole), the types of signatures, the names it defines.
void declaration_dependencies(const Unit &p_unit, std::vector<Dependency> &r_keys) {
	Collector collect(p_unit, r_keys);
	for (const Symbol *symbol : p_unit.declared) {
		if (!symbol->owner && !symbol->file_private) {
			r_keys.push_back(on_name(symbol->name));
			r_keys.push_back(on_defines(symbol->name));
		}
	}
	// Every type the signature pass resolves (names_signatures.cpp): accessors' too, which no symbol's node is.
	const auto signature = [&](const Node *p_node) {
		if (p_node->kind == NodeKind::FunctionDecl) {
			const FunctionDecl *function = static_cast<const FunctionDecl *>(p_node);
			for (const Parameter &parameter : function->parameters) {
				collect.type_ref(parameter.type);
			}
			collect.type_ref(function->return_type);
		} else if (p_node->kind == NodeKind::VarDecl) {
			for (const Declarator &declarator : static_cast<const VarDecl *>(p_node)->declarators) {
				collect.type_ref(declarator.type);
			}
		}
	};
	each_definition(p_unit, [&](Node *p_definition) {
		signature(p_definition);
		if (p_definition->kind == NodeKind::ClassDecl) {
			for (const Node *member : static_cast<const ClassDecl *>(p_definition)->members) {
				signature(member);
			}
		} else if (p_definition->kind == NodeKind::InterfaceDecl) {
			for (const FunctionDecl *method : static_cast<const InterfaceDecl *>(p_definition)->methods) {
				signature(method);
			}
		}
	});
	for (const Symbol *klass : p_unit.classes) {
		const auto supertype = [&](const TypeRef *p_type) {
			collect.type_ref(p_type);
			if (in_program(p_type ? p_type->symbol : nullptr)) {
				r_keys.push_back(on_shape(p_type->symbol));
			}
		};
		if (klass->node->kind == NodeKind::ClassDecl) {
			const ClassDecl *decl = static_cast<const ClassDecl *>(klass->node);
			supertype(decl->extends);
			for (const TypeRef *type : decl->implements) {
				supertype(type);
			}
			for (const Node *member : decl->members) {
				if (member->kind == NodeKind::Import) {
					collect.import(static_cast<const Import *>(member));
				}
			}
		} else {
			for (const TypeRef *type : static_cast<const InterfaceDecl *>(klass->node)->extends) {
				supertype(type);
			}
		}
	}
	collect.imports(p_unit.tree->imports);
	if (p_unit.tree->package) {
		collect.imports(p_unit.tree->package->imports);
	}
	for (const Import *import : p_unit.signature_imports) {
		collect.import(import);
	}
	for (const Unit *other : p_unit.halves_from) {
		r_keys.push_back(on_halves(other));
	}
	collect.cited();
}

// What the bodies read: every name, member and type written, and the type of every expression.
void body_dependencies(const Unit &p_unit, std::vector<Dependency> &r_keys) {
	Collector collect(p_unit, r_keys);
	each_definition(p_unit, [&](Node *p_definition) {
		const Symbol *klass = nullptr;
		if (p_definition->kind == NodeKind::ClassDecl) {
			klass = static_cast<ClassDecl *>(p_definition)->symbol;
		} else if (p_definition->kind == NodeKind::InterfaceDecl) {
			klass = static_cast<InterfaceDecl *>(p_definition)->symbol;
		}
		walk(p_definition, [&](Node *p_node) { collect.node(p_node, klass); });
	});
	collect.cited();
}

} // namespace kas3
