// What a file's check read from other files, worked out from what the check wrote onto its tree: the
// symbols its names found, the types its expressions have, and the names it looked up whether or not they
// found anything. Declaration files never change in place, so nothing of theirs is a dependency.
#include "dependencies.h"
#include "names.h"

#include "ast_walk.h"

#include <algorithm>
#include <array>
#include <unordered_map>

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
	// The same key comes again and again (every use of a field, every expression of one type): a small table of
	// the latest keys drops most repeats before they are stored and sorted.
	std::array<Dependency, 64> recent_{};
	// The program classes a lookup through a class may reach, by class: worked out once per file. Most
	// lookups in a row go through one class, which is kept at hand.
	std::unordered_map<const Symbol *, std::vector<const Symbol *>> chains_;
	const Symbol *last_class_ = nullptr;
	const std::vector<const Symbol *> *last_chain_ = nullptr;

	void push(const Dependency &p_key) {
		Dependency &slot = recent_[DependencyHash()(p_key) & (recent_.size() - 1)];
		if (!(slot == p_key)) {
			slot = p_key;
			keys_.push_back(p_key);
		}
	}

	// The class, its bases, and the interfaces of each, as far as they are the program's: a declaration file's
	// class extends only other declarations, which never change.
	const std::vector<const Symbol *> &chain(const Symbol *p_class) {
		if (p_class == last_class_) {
			return *last_chain_;
		}
		auto [found, added] = chains_.try_emplace(p_class);
		std::vector<const Symbol *> &chain = found->second;
		last_class_ = p_class;
		last_chain_ = &chain;
		if (!added) {
			return chain;
		}
		std::vector<const Symbol *> pending{ p_class };
		for (size_t i = 0; i < pending.size(); ++i) {
			if (!in_program(pending[i]) || !pending[i]->klass) {
				continue;
			}
			chain.push_back(pending[i]);
			const ClassInfo *info = pending[i]->klass;
			for (const Symbol *next : info->interfaces) {
				if (next && std::find(pending.begin(), pending.end(), next) == pending.end()) {
					pending.push_back(next);
				}
			}
			if (info->base && std::find(pending.begin(), pending.end(), info->base) == pending.end()) {
				pending.push_back(info->base);
			}
		}
		return chain;
	}

public:
	Collector(const Unit &p_unit, std::vector<Dependency> &r_keys) :
			unit_(p_unit), keys_(r_keys) {}

	void symbol(const Symbol *p_symbol) {
		if (in_program(p_symbol) && !local(p_symbol)) {
			push(on_symbol(p_symbol));
			if (p_symbol->klass) {
				push(on_chain(p_symbol));
			}
		}
	}

	void type(const Type *p_type) {
		for (; p_type; p_type = p_type->element) {
			if (in_program(p_type->symbol) && p_type->symbol->klass) {
				push(on_chain(p_type->symbol));
			}
		}
	}

	// A written type and its type arguments (`Vector.<Particle>`): each name is looked up.
	void type_ref(const TypeRef *p_type) {
		for (; p_type; p_type = p_type->argument) {
			if (p_type->name.count) {
				push(on_name(p_type->name[p_type->name.count - 1]));
				symbol(p_type->symbol);
			}
		}
	}

	void import(const Import *p_import) {
		if (p_import->name.count == 0) {
			return;
		}
		if (p_import->wildcard) {
			push(on_package(dotted_name(p_import->name, p_import->name.count)));
		} else {
			push(on_name(p_import->name[p_import->name.count - 1]));
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
	void members(const Symbol *p_class, uint64_t p_name) {
		if (!in_program(p_class)) {
			return;
		}
		for (const Symbol *klass : chain(p_class)) {
			push({ DependencyKind::Member, klass, p_name });
		}
	}

	void cited() {
		for (const Unit *other : unit_.cited) {
			push(on_positions(other));
		}
	}

	void node(Node *p_node, const Symbol *p_class) {
		switch (p_node->kind) {
			case NodeKind::Name: {
				const Name *name = static_cast<const Name *>(p_node);
				if (name->symbol && local(name->symbol)) {
					break;
				}
				const uint64_t hash = name_hash(name->text);
				push({ DependencyKind::Name, nullptr, hash });
				members(p_class, hash);
				symbol(name->symbol);
				if (in_program(name->symbol) && name->symbol->klass) {
					push(on_member(name->symbol, std::string_view())); // `new C()`: its constructor.
				}
			} break;
			case NodeKind::Member: {
				const Member *member = static_cast<const Member *>(p_node);
				const Type *object = member->object ? member->object->type : nullptr;
				if (object && (object->kind == Type::Instance || object->kind == Type::ClassObject)) {
					members(object->kind == Type::Instance ? object->symbol : object->element ? object->element->symbol : nullptr,
							name_hash(member->name));
				} else {
					push(on_name(member->name)); // A package path, or a value of no known type.
				}
				const Symbol *found = member->resolved();
				symbol(found);
				if (in_program(found) && found->klass) {
					push(on_member(found, std::string_view()));
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

template <class F>
void each_definition(const Unit &p_unit, F &&p_visit) {
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
