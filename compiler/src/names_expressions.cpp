// The bodies pass: expressions. A bare name is looked up in the scopes of the enclosing functions from the
// inside out, then in the class and its bases, then at package level.
#include "names_resolver.h"

namespace kas3 {

Symbol *Resolver::lookup(std::string_view p_name, uint32_t p_offset, bool p_report) {
	for (Scope *scope = scope_; scope; scope = scope->parent) {
		auto found = scope->names.find(p_name);
		if (found != scope->names.end()) {
			return found->second;
		}
	}
	Symbol *hidden = nullptr;
	Symbol *instance = nullptr; // An instance member that static code can't reach.
	if (context_.klass && context_.klass->kind == SymbolKind::Class) {
		Symbol *member = compilation_.find_member(context_.klass, p_name, static_, &instance, &hidden);
		if (member) {
			return member;
		}
	}
	Found found = compilation_.lookup_package_level(context_, p_name);
	if (found.first && !found.second) {
		return found.first;
	}
	if (!p_report) {
		return nullptr;
	}
	if (found.second) {
		error(p_offset, compilation_.ambiguous_message(p_name, found));
	} else if (instance) {
		error(p_offset, "can't use the instance member " + quoted(p_name) + " in a static context");
	} else if (found.suppressed) {
		// A failed import has been reported, and may be where this name was to come from.
	} else if (hidden) {
		if (hidden->modifiers & Modifiers::Private) {
			unresolved(p_offset, quoted(p_name) + " is private in " + std::string(hidden->owner->name));
		} else {
			unresolved(p_offset, quoted(p_name) + " in " + std::string(hidden->owner->name) +
									" is not visible from this package");
		}
	} else if (!(context_.klass && compilation_.hierarchy_broken(context_.klass))) {
		// A base class that failed to resolve may be where the name comes from.
		unresolved(p_offset, "undefined name " + quoted(p_name));
	}
	return nullptr;
}

void Resolver::assignable(Expr *p_target) {
	if (!p_target) {
		return;
	}
	switch (p_target->kind) {
		case NodeKind::Name:
		case NodeKind::Member:
		case NodeKind::Index:
			break;
		default:
			error(p_target->offset, "can't assign to this expression");
	}
}

void Resolver::name(Name *p_name) {
	if (p_name->text.empty()) {
		return; // A placeholder for an expression the parser already reported as missing.
	}
	p_name->symbol = lookup(p_name->text, p_name->offset, true);
	if (p_name->symbol) {
		record(p_name->offset, p_name->text, p_name->symbol);
	}
}

// `a.b.c`: either a value `a` with members, or a definition named by its package.
void Resolver::member(Member *p_member) {
	std::vector<Member *> chain; // Outermost first.
	Expr *root = p_member;
	while (root->kind == NodeKind::Member) {
		chain.push_back(static_cast<Member *>(root));
		root = static_cast<Member *>(root)->object;
	}
	if (!root) {
		return;
	}
	if (root->kind != NodeKind::Name) {
		expression(root);
		return;
	}
	Name *first = static_cast<Name *>(root);
	if (first->text.empty()) {
		return;
	}
	first->symbol = lookup(first->text, first->offset, false);
	if (first->symbol) {
		record(first->offset, first->text, first->symbol);
		return;
	}
	// Not a value. The longest leading run that names a package, followed by a definition in it.
	std::string package(first->text);
	Member *best = nullptr;
	Symbol *best_symbol = nullptr;
	for (size_t i = chain.size(); i-- > 0;) {
		Member *link = chain[i];
		if (compilation_.find_package(package)) {
			Found found = compilation_.lookup_qualified(context_, package, link->name);
			if (found.first) {
				best = link;
				best_symbol = found.first;
			}
		}
		package += '.';
		package += link->name;
	}
	if (best) {
		best->symbol = best_symbol;
		record(best->name_offset, best->name, best_symbol);
		return;
	}
	name(first); // Reports it.
}

void Resolver::expressions(const List<Expr *> &p_list) {
	for (Expr *item : p_list) {
		expression(item);
	}
}

void Resolver::expression(Expr *p_expression) {
	if (!p_expression) {
		return;
	}
	switch (p_expression->kind) {
		case NodeKind::Name:
			name(static_cast<Name *>(p_expression));
			break;
		case NodeKind::ThisExpr:
			if (static_) {
				error(p_expression->offset, "'this' can't be used in a static context");
			}
			break;
		case NodeKind::SuperExpr:
			if (static_ || !context_.klass || context_.klass->kind != SymbolKind::Class) {
				error(p_expression->offset, "'super' can only be used in an instance method");
			}
			break;
		case NodeKind::ArrayLit:
			expressions(static_cast<ArrayLit *>(p_expression)->elements);
			break;
		case NodeKind::ObjectLit:
			for (ObjectField &field : static_cast<ObjectLit *>(p_expression)->fields) {
				expression(field.value);
			}
			break;
		case NodeKind::VectorLit:
			type(static_cast<VectorLit *>(p_expression)->element_type);
			expressions(static_cast<VectorLit *>(p_expression)->elements);
			break;
		case NodeKind::FunctionExpr:
			function(static_cast<FunctionExpr *>(p_expression)->function, true);
			break;
		case NodeKind::Member:
			member(static_cast<Member *>(p_expression));
			break;
		case NodeKind::Index:
			expression(static_cast<Index *>(p_expression)->object);
			expression(static_cast<Index *>(p_expression)->index);
			break;
		case NodeKind::TypeApply:
			expression(static_cast<TypeApply *>(p_expression)->base);
			type(static_cast<TypeApply *>(p_expression)->argument);
			break;
		case NodeKind::Call:
			expression(static_cast<Call *>(p_expression)->callee);
			expressions(static_cast<Call *>(p_expression)->arguments);
			break;
		case NodeKind::New:
			expression(static_cast<New *>(p_expression)->callee);
			expressions(static_cast<New *>(p_expression)->arguments);
			break;
		case NodeKind::Unary: {
			Unary *unary = static_cast<Unary *>(p_expression);
			if (unary->op == TokenKind::PlusPlus || unary->op == TokenKind::MinusMinus) {
				assignable(unary->operand);
			}
			expression(unary->operand);
		} break;
		case NodeKind::Postfix:
			assignable(static_cast<Postfix *>(p_expression)->operand);
			expression(static_cast<Postfix *>(p_expression)->operand);
			break;
		case NodeKind::Binary:
			expression(static_cast<Binary *>(p_expression)->left);
			expression(static_cast<Binary *>(p_expression)->right);
			break;
		case NodeKind::Assign:
			assignable(static_cast<Assign *>(p_expression)->target);
			expression(static_cast<Assign *>(p_expression)->target);
			expression(static_cast<Assign *>(p_expression)->value);
			break;
		case NodeKind::Conditional:
			expression(static_cast<Conditional *>(p_expression)->condition);
			expression(static_cast<Conditional *>(p_expression)->then_value);
			expression(static_cast<Conditional *>(p_expression)->else_value);
			break;
		default:
			break;
	}
}

} // namespace kas3
