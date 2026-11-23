// A local can use its initial type only when every use has the same meaning with that type.
#include "ast_walk.h"
#include "typer.h"

namespace kas3 {
namespace {

using Parents = std::unordered_map<Node *, Node *>;

Node *parent(const Parents &p_parents, Node *p_node) {
	const auto at = p_parents.find(p_node);
	return at == p_parents.end() ? nullptr : at->second;
}

// These operations on a freshly constructed, detached Node2D neither publish it nor attach a script.
// Other operations can expose even a local with no AS3 aliases (addToGroup, for example).
bool isolated_transform(const Symbol &p_member) {
	if (p_member.owner->name != "Node2D") {
		return false;
	}
	const auto &name = p_member.name;
	return name == "position" || name == "rotation" || name == "rotationDegrees" || name == "scale" ||
			name == "skew" || name == "transform" || name == "rotate" || name == "translate" || name == "applyScale";
}

struct Binding {
	Member *access;
	Symbol *member;
	Expr *operation;
	const Type *result;
};

bool discarded(const Parents &p_parents, Expr *p_expr) {
	Node *use = parent(p_parents, p_expr);
	return use && use->kind == NodeKind::ExprStmt;
}

const Type *initializer_type(Compilation &p_compilation, Node *p_parent, Expr *p_expr) {
	if (p_parent && p_parent->kind == NodeKind::VarDecl) {
		for (const auto &decl : static_cast<VarDecl *>(p_parent)->declarators) {
			if (decl.initializer == p_expr && decl.type) {
				return p_compilation.type_of(decl.type);
			}
		}
	}
	return nullptr;
}

std::optional<Binding> isolated_use(Compilation &p_compilation, const Type *p_type, Name *p_name, const Parents &p_parents, Block *p_body) {
	Node *use = parent(p_parents, p_name);
	if (!use || use->kind != NodeKind::Member) {
		return std::nullopt;
	}
	auto *access = static_cast<Member *>(use);
	Symbol *member = p_compilation.find_member(p_type->symbol, access->name, false, nullptr, nullptr);
	if (!member || member->is_static() || !(member->modifiers & Modifiers::Public)) {
		return std::nullopt;
	}
	Node *operation = parent(p_parents, access);
	if (!operation) {
		return std::nullopt;
	}
	const bool frees = member->owner->name == "GodotObject" && member->name == "free";
	if (!isolated_transform(*member) && !frees) {
		return std::nullopt;
	}
	if (member->kind == SymbolKind::Property && member->getter) {
		const Type *result = p_compilation.type_of(member->getter->return_type);
		if (initializer_type(p_compilation, operation, access) == result) {
			return Binding{ access, member, access, result };
		}
		if (operation->kind != NodeKind::Assign || !member->setter) {
			return std::nullopt;
		}
		auto *assign = static_cast<Assign *>(operation);
		Expr *given = assign->value;
		if (assign->target == access && assign->op == TokenKind::Assign && discarded(p_parents, assign) &&
				given->type == result && (given->kind == NodeKind::New || given->kind == NodeKind::NumberLit)) {
			return Binding{ access, member, assign, result };
		}
	}
	if (member->kind == SymbolKind::Method && operation->kind == NodeKind::Call) {
		auto *call = static_cast<Call *>(operation);
		auto *fn = static_cast<FunctionDecl *>(member->node);
		if (call->callee != access || !discarded(p_parents, call) || call->arguments.count != fn->parameters.count ||
				!fn->return_type || fn->return_type->form != TypeRef::Void) {
			return std::nullopt;
		}
		for (uint32_t i = 0; i < call->arguments.count; i++) {
			const Expr *arg = call->arguments[i];
			if (arg->type != p_compilation.type_of(fn->parameters[i].type) ||
					(arg->kind != NodeKind::NumberLit && arg->kind != NodeKind::New)) {
				return std::nullopt;
			}
		}
		if (frees && parent(p_parents, parent(p_parents, call)) != p_body) {
			return std::nullopt;
		}
		return Binding{ access, member, call, p_compilation.void_type() };
	}
	return std::nullopt;
}

} // namespace

void Typer::prove_local_uses(Block *p_body, const std::unordered_map<Symbol *, Expr *> &p_initial) {
	Parents parents;
	std::unordered_map<Symbol *, std::vector<Name *>> uses;
	walk(p_body, [&](Node *node) {
		if (node->kind == NodeKind::FunctionDecl) {
			return false;
		}
		walk(node, [&](Node *child) {
			if (child != node) {
				parents.emplace(child, node);
				return false;
			}
			return true;
		});
		if (node->kind == NodeKind::Name) {
			auto *name = static_cast<Name *>(node);
			if (p_initial.count(name->symbol)) {
				uses[name->symbol].push_back(name);
			}
		}
		return true;
	});
	for (const auto &[local, initial] : p_initial) {
		const Type *type = initial->type;
		const bool engine = godot_ && type->symbol->package == godot_ && type->symbol->unit->is_declaration;
		const bool fresh = engine && type->symbol->name == "Node2D" && initial->kind == NodeKind::New;
		std::vector<Binding> bindings;
		bool safe = true;
		for (Name *name : uses[local]) {
			if (fresh) {
				auto binding = isolated_use(compilation_, type, name, parents, p_body);
				if (!binding || (!bindings.empty() && bindings.back().member->name == "free")) {
					safe = false;
					break;
				}
				bindings.push_back(*binding);
			} else {
				Node *use = parent(parents, name);
				if (use && use->kind == NodeKind::Assign && static_cast<Assign *>(use)->target == name &&
						discarded(parents, static_cast<Assign *>(use))) {
					continue;
				}
				if (initializer_type(compilation_, use, name) == type ||
						(use && use->kind == NodeKind::Return && current_ && compilation_.type_of(current_->return_type) == type)) {
					continue;
				}
				if (type == int_ && use && use->kind == NodeKind::Call &&
						static_cast<Call *>(use)->callee->kind == NodeKind::Name &&
						static_cast<Name *>(static_cast<Call *>(use)->callee)->symbol == compilation_.builtin("trace")) {
					continue;
				}
				safe = false;
				break;
			}
		}
		if (!safe) {
			continue;
		}
		local->storage = type;
		for (Name *name : uses[local]) {
			name->type = type;
		}
		for (const Binding &binding : bindings) {
			binding.access->member = binding.member;
			binding.access->type = symbol_type(binding.member, nullptr);
			binding.operation->type = binding.result;
		}
	}
}

} // namespace kas3
