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

// The API generator records constructor forms on native value classes.
bool native_value(const Symbol *p_class, Package *p_godot) {
	if (!p_class || !p_class->unit || !p_class->unit->is_declaration || p_class->package != p_godot ||
			!p_class->node || p_class->node->kind != NodeKind::ClassDecl) {
		return false;
	}
	for (const auto &tag : static_cast<ClassDecl *>(p_class->node)->metadata) {
		if (tag.name == "Constructor") {
			return true;
		}
	}
	return false;
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

// Only a dominating construction proves that a nullable math argument has a value.
bool exact_argument(Expr *p_expr, const Type *p_type, const Parents &p_parents, Block *p_body) {
	if (p_expr->type != p_type) {
		return false;
	}
	if (p_expr->kind == NodeKind::New || p_expr->kind == NodeKind::NumberLit || p_expr->kind == NodeKind::BoolLit || p_expr->kind == NodeKind::StringLit) {
		return true;
	}
	if (p_expr->kind != NodeKind::Name) {
		return false;
	}
	const auto *symbol = static_cast<Name *>(p_expr)->symbol;
	if (!symbol || !symbol->is_const || symbol->kind != SymbolKind::Local || !symbol->node || symbol->node->kind != NodeKind::VarDecl ||
			parent(p_parents, parent(p_parents, symbol->node)) != p_body) {
		return false;
	}
	for (const auto &decl : static_cast<VarDecl *>(symbol->node)->declarators) {
		if (decl.symbol == symbol && decl.initializer && decl.initializer->end <= p_expr->offset &&
				decl.initializer->kind == NodeKind::New && decl.initializer->type == p_type) {
			return true;
		}
	}
	return false;
}

// Types tentatively proved for enclosing expressions are published only if every use succeeds.
using Facts = std::unordered_map<Expr *, const Type *>;

struct Consumers {
	Compilation &compilation;
	const Parents &parents;
	const Type *returns;
	const Type *number;
	const Type *boolean;
	const Facts &assumptions;
	Facts results;

	const Type *operand(Expr *p_expr) const {
		if (p_expr->kind == NodeKind::Binary && static_cast<Binary *>(p_expr)->op == TokenKind::KwAs) {
			return nullptr; // A failed numeric cast still holds null.
		}
		const auto at = assumptions.find(p_expr);
		return at == assumptions.end() ? p_expr->type : at->second;
	}

	bool ordinary_number(const Type *p_type) const {
		return p_type == number || (p_type && p_type->symbol &&
				(p_type->symbol == compilation.builtin("int") || p_type->symbol == compilation.builtin("uint")));
	}

	bool argument(Call *p_call, Expr *p_expr, const Type *p_type) const {
		Expr *callee = p_call->callee;
		const Symbol *symbol = callee->kind == NodeKind::Name ? static_cast<Name *>(callee)->symbol :
				callee->kind == NodeKind::Member ? static_cast<Member *>(callee)->resolved() : nullptr;
		for (uint32_t i = 0; i < p_call->arguments.count; i++) {
			if (p_call->arguments[i] != p_expr) {
				continue;
			}
			if (callee->type && callee->type->kind == Type::Callback) {
				return i < callee->type->parameters.size() && callee->type->parameters[i] == p_type;
			}
			if (symbol && symbol->node && symbol->node->kind == NodeKind::FunctionDecl) {
				const auto &parameters = static_cast<FunctionDecl *>(symbol->node)->parameters;
				return i < parameters.count && !parameters[i].is_rest && compilation.type_of(parameters[i].type) == p_type;
			}
		}
		return false;
	}

	const Type *binary_result(Binary *p_binary, Expr *p_expr, const Type *p_type) const {
		const Type *other = operand(p_binary->left == p_expr ? p_binary->right : p_binary->left);
		if (p_type == boolean && other == boolean && (p_binary->op == TokenKind::AmpAmp || p_binary->op == TokenKind::PipePipe)) {
			return boolean;
		}
		if (!ordinary_number(p_type) || !ordinary_number(other)) {
			return nullptr;
		}
		switch (p_binary->op) {
			case TokenKind::Plus: case TokenKind::Minus: case TokenKind::Star: case TokenKind::Slash: case TokenKind::Percent:
				return number;
			case TokenKind::Less: case TokenKind::LessEqual: case TokenKind::Greater: case TokenKind::GreaterEqual:
			case TokenKind::Equal: case TokenKind::NotEqual: case TokenKind::StrictEqual: case TokenKind::StrictNotEqual:
				return boolean;
			default:
				return nullptr;
		}
	}

	bool receives(Expr *p_expr, const Type *p_type) {
		Node *use = parent(parents, p_expr);
		if (!use || !p_type) {
			return false;
		}
		switch (use->kind) {
			case NodeKind::Return:
				return returns == p_type || (ordinary_number(returns) && ordinary_number(p_type));
			case NodeKind::Call:
				return argument(static_cast<Call *>(use), p_expr, p_type);
			case NodeKind::If: case NodeKind::While: case NodeKind::DoWhile:
				return p_type == boolean || ordinary_number(p_type);
			case NodeKind::For:
				return static_cast<For *>(use)->condition == p_expr && (p_type == boolean || ordinary_number(p_type));
			case NodeKind::Conditional:
				return static_cast<Conditional *>(use)->condition == p_expr && (p_type == boolean || ordinary_number(p_type));
			case NodeKind::Binary: {
				auto *binary = static_cast<Binary *>(use);
				const Type *result = binary_result(binary, p_expr, p_type);
				if (result && (binary->type == result || receives(binary, result))) {
					results[binary] = result;
					return true;
				}
				return false;
			}
			case NodeKind::Assign: {
				auto *assign = static_cast<Assign *>(use);
				if (assign->value != p_expr) {
					return false;
				}
				if (assign->op == TokenKind::Assign) {
					return assign->target->type == p_type;
				}
				return ordinary_number(p_type) && ordinary_number(operand(assign->target)) &&
						(assign->op == TokenKind::PlusAssign || assign->op == TokenKind::MinusAssign ||
						assign->op == TokenKind::StarAssign || assign->op == TokenKind::SlashAssign || assign->op == TokenKind::PercentAssign);
			}
			default:
				return initializer_type(compilation, use, p_expr) == p_type;
		}
	}
};

std::optional<Binding> member_use(Compilation &p_compilation, const Type *p_type, Name *p_name, const Parents &p_parents, Block *p_body, Consumers &p_consumers, bool p_write) {
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
	if (member->kind == SymbolKind::Field || (member->kind == SymbolKind::Property && member->getter)) {
		const Type *result = p_compilation.type_of(member->getter ? member->getter->return_type : member->type);
		if (p_consumers.receives(access, result)) {
			return Binding{ access, member, access, result };
		}
		if (!p_write || operation->kind != NodeKind::Assign || (member->kind == SymbolKind::Property && !member->setter) || member->is_const) {
			return std::nullopt;
		}
		auto *assign = static_cast<Assign *>(operation);
		if (assign->target == access && assign->op == TokenKind::Assign && discarded(p_parents, assign) &&
				exact_argument(assign->value, result, p_parents, p_body)) {
			return Binding{ access, member, assign, result };
		}
	}
	if (member->kind == SymbolKind::Method && operation->kind == NodeKind::Call) {
		auto *call = static_cast<Call *>(operation);
		auto *fn = static_cast<FunctionDecl *>(member->node);
		const Type *result = p_compilation.type_of(fn->return_type);
		if (call->callee != access || call->arguments.count != fn->parameters.count || (!discarded(p_parents, call) && !p_consumers.receives(call, result))) {
			return std::nullopt;
		}
		for (uint32_t i = 0; i < call->arguments.count; i++) {
			if (!exact_argument(call->arguments[i], p_compilation.type_of(fn->parameters[i].type), p_parents, p_body)) {
				return std::nullopt;
			}
		}
		return Binding{ access, member, call, result };
	}
	return std::nullopt;
}

using Uses = std::unordered_map<Symbol *, std::vector<Name *>>;

void index_uses(Block *p_body, const std::unordered_map<Symbol *, Expr *> &p_initial, Parents &r_parents, Uses &r_uses) {
	walk(p_body, [&](Node *p_node) {
		if (p_node->kind == NodeKind::FunctionDecl) {
			return false;
		}
		walk(p_node, [&](Node *p_child) {
			if (p_child != p_node) {
				r_parents.emplace(p_child, p_node);
				return false;
			}
			return true;
		});
		if (p_node->kind == NodeKind::Name) {
			auto *name = static_cast<Name *>(p_node);
			if (p_initial.count(name->symbol)) {
				r_uses[name->symbol].push_back(name);
			}
		}
		return true;
	});
}

bool loop(NodeKind p_kind) {
	return p_kind == NodeKind::For || p_kind == NodeKind::ForIn || p_kind == NodeKind::While || p_kind == NodeKind::DoWhile;
}

// Include effects on the back edge when the receiver was created outside the loop.
uint32_t last_iteration(const Parents &p_parents, Expr *p_initial, Expr *p_access) {
	uint32_t end = p_access->end;
	for (Node *node = p_access; node; node = parent(p_parents, node)) {
		if (loop(node->kind) && node->offset > p_initial->end) {
			end = std::max(end, node->end);
		}
	}
	return end;
}

// The first operation that can expose the receiver or execute user code ends its proof.
// An operation can itself bind before it publishes the receiver. Later statements cannot
// affect an earlier access unless control can return to it through a loop.
uint32_t first_effect(Block *p_body, Expr *p_initial, const std::vector<Binding> &p_bindings, Package *p_godot, const Consumers &p_consumers) {
	uint32_t first = p_body->end;
	std::unordered_set<Node *> known{ p_initial, static_cast<New *>(p_initial)->callee };
	for (const Binding &binding : p_bindings) {
		known.insert(binding.access->object);
		known.insert(binding.access);
		known.insert(binding.operation);
		if (binding.member->kind != SymbolKind::Property) {
			first = std::min(first, binding.operation->end);
		}
	}
	walk(p_body, [&](Node *node) {
		if (node->end <= p_initial->offset || node->offset >= first) {
			return false;
		}
		if (known.count(node) || node->kind == NodeKind::TypeRef) {
			return true;
		}
		if (node->kind == NodeKind::New) {
			auto *value = static_cast<New *>(node);
			const auto *klass = value->type ? value->type->symbol : nullptr;
			if (native_value(klass, p_godot)) {
				known.insert(value->callee);
				return true;
			}
		} else if (node->kind == NodeKind::Name) {
			const auto *name = static_cast<Name *>(node);
			if (name->symbol && (name->symbol->kind == SymbolKind::Local || name->symbol->kind == SymbolKind::Parameter) &&
					(p_consumers.ordinary_number(p_consumers.operand(static_cast<Name *>(node))) || name->type == p_consumers.boolean ||
					(name->type && native_value(name->type->symbol, p_godot)))) {
				return true;
			}
		} else if (node->kind == NodeKind::Member) {
			const auto *member = static_cast<Member *>(node)->member;
			if (member && member->kind == SymbolKind::Field && !member->is_static() && native_value(member->owner, p_godot)) {
				return true;
			}
		} else if ((node->kind < NodeKind::NumberLit || node->kind > NodeKind::Conditional ||
				node->kind == NodeKind::NumberLit || node->kind == NodeKind::StringLit || node->kind == NodeKind::BoolLit ||
				node->kind == NodeKind::Binary || node->kind == NodeKind::Assign || node->kind == NodeKind::Postfix || node->kind == NodeKind::Unary) &&
				node->kind != NodeKind::ForIn && node->kind != NodeKind::FunctionDecl) {
			return true;
		}
		first = std::min(first, node->offset);
		return false;
	});
	return first;
}

void retain_detached(const Parents &p_parents, Expr *p_initial, uint32_t p_effect, uint32_t p_escape, std::vector<Binding> &r_bindings) {
	const uint32_t end = std::min(p_effect, p_escape);
	r_bindings.erase(std::remove_if(r_bindings.begin(), r_bindings.end(), [&](const Binding &p_bind) {
		return last_iteration(p_parents, p_initial, p_bind.operation) > end;
	}), r_bindings.end());
}

bool direct_use(Consumers &p_consumers, Name *p_name, const Type *p_type) {
	Node *use = parent(p_consumers.parents, p_name);
	if (use && use->kind == NodeKind::Assign && static_cast<Assign *>(use)->target == p_name) {
		auto *assign = static_cast<Assign *>(use);
		if (!discarded(p_consumers.parents, assign)) {
			return false;
		}
		if (assign->op == TokenKind::Assign) {
			return true;
		}
		const Type *right = p_consumers.operand(assign->value);
		return p_type == p_consumers.number && p_consumers.ordinary_number(right) &&
				p_consumers.receives(assign->value, right);
	}
	if (use && use->kind == NodeKind::Call && p_type->symbol == p_consumers.compilation.builtin("int")) {
		Expr *callee = static_cast<Call *>(use)->callee;
		if (callee->kind == NodeKind::Name && static_cast<Name *>(callee)->symbol == p_consumers.compilation.builtin("trace")) {
			return true;
		}
	}
	return p_consumers.receives(p_name, p_type);
}

struct LocalProof {
	bool storage = false;
	std::vector<Binding> bindings;
	Facts results;
};

LocalProof check_uses(Block *p_body, Expr *p_initial, const Type *p_type, bool p_fresh, Package *p_godot,
		const std::vector<Name *> &p_uses, Consumers &p_consumers, bool p_numeric) {
	Symbol *klass = p_type->symbol;
	const bool math = native_value(klass, p_godot);
	const bool pure_class = klass && klass->unit && !klass->unit->is_declaration && !(klass->modifiers & Modifiers::Dynamic);
	const bool can_bind = p_fresh || math || pure_class || p_numeric || p_type == p_consumers.boolean || klass == p_consumers.compilation.builtin("String");

	std::vector<Binding> bindings;
	bool safe = true, assigned = false;
	uint32_t escape = p_body->end;
	for (Name *name : p_uses) {
		Node *use = parent(p_consumers.parents, name);
		if (!p_fresh && direct_use(p_consumers, name, p_type)) {
			assigned |= use->kind == NodeKind::Assign && static_cast<Assign *>(use)->target == name;
			continue;
		}
		auto bind = can_bind ? member_use(p_consumers.compilation, p_type, name, p_consumers.parents, p_body, p_consumers, p_fresh || pure_class) : std::nullopt;
		if (!bind) {
			safe = false;
			if (p_fresh) {
				escape = std::min(escape, name->offset);
				continue;
			}
			break;
		}
		bindings.push_back(*bind);
	}
	if (p_fresh) {
		retain_detached(p_consumers.parents, p_initial, first_effect(p_body, p_initial, bindings, p_godot, p_consumers), escape, bindings);
		safe = bindings.size() == p_uses.size();
	} else if (!safe || (!bindings.empty() && (assigned || (pure_class && p_initial->kind != NodeKind::New && !(klass->modifiers & Modifiers::Final))))) {
		return {};
	}
	return { safe, std::move(bindings), safe ? std::move(p_consumers.results) : Facts{} };
}

} // namespace

void Typer::prove_local_uses(Block *p_body, const std::unordered_map<Symbol *, Expr *> &p_initial) {
	Parents parents;
	Uses uses;
	index_uses(p_body, p_initial, parents, uses);
	const Type *returns = current_ ? compilation_.type_of(current_->return_type) : nullptr;
	const auto engine_root = godot_ ? find_in(godot_->definitions, "GodotObject") : nullptr;
	struct Local {
		Symbol *symbol;
		Expr *initial;
		const Type *type;
		bool fresh;
	};
	std::vector<Local> locals;
	Facts assumptions;
	for (const auto &[symbol, initial] : p_initial) {
		const Type *type = initial->type;
		Symbol *klass = type->symbol;
		const bool engine = klass && engine_root && derives_from(klass, engine_root);
		if (engine && (!klass->unit->is_declaration || initial->kind != NodeKind::New || static_cast<New *>(initial)->arguments.count)) {
			continue;
		}
		for (Name *name : uses[symbol]) {
			Node *use = parent(parents, name);
			if (use && use->kind == NodeKind::Assign) {
				auto *assign = static_cast<Assign *>(use);
				if (assign->target == name && assign->op != TokenKind::Assign && (type == int_ || type == uint_)) {
					type = number_; // Compound arithmetic produces a Number, including on overflow.
				}
			}
		}
		locals.push_back({ symbol, initial, type, engine });
		for (Name *name : uses[symbol]) {
			assumptions.emplace(name, type);
			Node *use = parent(parents, name);
			if (klass && use && use->kind == NodeKind::Member) {
				auto *access = static_cast<Member *>(use);
				auto *member = compilation_.find_member(klass, access->name, false, nullptr, nullptr);
				if (member && (member->kind == SymbolKind::Field || member->getter)) {
					assumptions.emplace(access, compilation_.type_of(member->getter ? member->getter->return_type : member->type));
				}
			}
		}
	}

	// Start with every candidate. Withdraw unsupported assumptions together so that two
	// independently initialized operands can prove each other, irrespective of visit order.
	for (;;) {
		Facts supported;
		std::vector<LocalProof> proofs;
		for (const Local &local : locals) {
			Consumers consumers{ compilation_, parents, returns, number_, boolean_, assumptions, {} };
			auto proof = check_uses(p_body, local.initial, local.type, local.fresh, godot_, uses[local.symbol], consumers, numeric(local.type));
			if (proof.storage) {
				for (Name *name : uses[local.symbol]) {
					supported.emplace(name, local.type);
				}
			}
			for (const Binding &binding : proof.bindings) {
				supported.emplace(binding.access->object, local.type);
				supported.emplace(binding.access, binding.result);
			}
			proofs.push_back(std::move(proof));
		}
		const auto before = assumptions.size();
		for (auto it = assumptions.begin(); it != assumptions.end();) {
			it = supported.count(it->first) ? std::next(it) : assumptions.erase(it);
		}
		if (before != assumptions.size()) {
			continue;
		}
		for (size_t i = 0; i < locals.size(); i++) {
			const Local &local = locals[i];
			const auto &proof = proofs[i];
			if (proof.storage) {
				local.symbol->storage = local.type;
				for (Name *name : uses[local.symbol]) {
					name->type = local.type;
				}
				for (const auto &[expr, result] : proof.results) {
					expr->type = result;
				}
			}
			for (const Binding &binding : proof.bindings) {
				binding.access->object->type = local.type;
				binding.access->member = binding.member;
				binding.access->type = symbol_type(binding.member, nullptr);
				binding.operation->type = binding.result;
			}
		}
		return;
	}
}

} // namespace kas3
