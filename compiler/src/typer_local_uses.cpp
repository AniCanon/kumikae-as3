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
struct Consumers {
	Compilation &compilation;
	const Parents &parents;
	const Type *returns;
	const Type *number;
	const Type *boolean;
	std::unordered_map<Expr *, const Type *> results;

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
		const Type *other = (p_binary->left == p_expr ? p_binary->right : p_binary->left)->type;
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
				return returns == p_type;
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
				return ordinary_number(p_type) && ordinary_number(assign->target->type) &&
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

void index_uses(Block *p_body, const std::unordered_map<Symbol *, Expr *> &p_initial, Parents &parents, Uses &uses) {
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
uint32_t first_effect(Block *p_body, Expr *p_initial, const std::vector<Binding> &p_bindings, Package *p_godot) {
	uint32_t first = p_body->end;
	std::unordered_set<Node *> known{ p_initial, static_cast<New *>(p_initial)->callee };
	for (const Binding &binding : p_bindings) {
		known.insert(binding.access->object);
		known.insert(binding.access);
		known.insert(binding.operation);
		const bool read = binding.operation == binding.access && binding.member->kind == SymbolKind::Property;
		if (!read && !isolated_transform(*binding.member)) {
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
			if (klass && klass->package == p_godot && klass->unit->is_declaration && (klass->name == "Vector2" || klass->name == "Color")) {
				known.insert(value->callee);
				return true;
			}
		} else if (node->kind == NodeKind::Name) {
			const auto *name = static_cast<Name *>(node);
			if (name->symbol && (name->symbol->kind == SymbolKind::Local || name->symbol->kind == SymbolKind::Parameter)) {
				return true;
			}
		} else if (node->kind == NodeKind::Member) {
			const auto *member = static_cast<Member *>(node)->member;
			if (member && member->owner && member->owner->package == p_godot && member->owner->name == "Vector2" && (member->name == "x" || member->name == "y")) {
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

bool direct_use(Consumers &p_consumers, Name *p_name, const Type *p_type) {
	Node *use = parent(p_consumers.parents, p_name);
	if (use && use->kind == NodeKind::Assign && static_cast<Assign *>(use)->target == p_name) {
		return discarded(p_consumers.parents, static_cast<Assign *>(use));
	}
	if (use && use->kind == NodeKind::Call && p_type->symbol == p_consumers.compilation.builtin("int")) {
		Expr *callee = static_cast<Call *>(use)->callee;
		if (callee->kind == NodeKind::Name && static_cast<Name *>(callee)->symbol == p_consumers.compilation.builtin("trace")) {
			return true;
		}
	}
	return p_consumers.receives(p_name, p_type);
}

} // namespace

void Typer::prove_local_uses(Block *p_body, const std::unordered_map<Symbol *, Expr *> &p_initial) {
	Parents parents;
	Uses uses;
	index_uses(p_body, p_initial, parents, uses);
	const Type *returns = current_ ? compilation_.type_of(current_->return_type) : nullptr;
	const auto engine_root = godot_ ? find_in(godot_->definitions, "GodotObject") : nullptr;
	for (const auto &[local, initial] : p_initial) {
		const Type *type = initial->type;
		Symbol *klass = type->symbol;
		const bool engine_object = klass && engine_root && derives_from(klass, engine_root);
		const bool fresh = engine_object && klass->unit->is_declaration && initial->kind == NodeKind::New && !static_cast<New *>(initial)->arguments.count;
		if (engine_object && !fresh) {
			continue;
		}
		const bool math = klass && klass->package == godot_ && klass->unit && klass->unit->is_declaration && (klass->name == "Vector2" || klass->name == "Color");
		const bool pure_class = klass && klass->unit && !klass->unit->is_declaration && !(klass->modifiers & Modifiers::Dynamic);
		const bool can_bind = fresh || math || pure_class || type == string_ || numeric(type) || type == boolean_;
		Consumers consumers{ compilation_, parents, returns, number_, boolean_, {} };
		std::vector<Binding> bindings;
		bool safe = true, assigned = false;
		for (Name *name : uses[local]) {
			Node *use = parent(parents, name);
			if (!fresh && direct_use(consumers, name, type)) {
				assigned |= use->kind == NodeKind::Assign && static_cast<Assign *>(use)->target == name;
				continue;
			}
			auto bind = can_bind ? member_use(compilation_, type, name, parents, p_body, consumers, fresh || pure_class) : std::nullopt;
			if (!bind) {
				safe = false;
				if (fresh) {
					continue;
				}
				break;
			}
			bindings.push_back(*bind);
		}
		if (fresh) {
			uint32_t end = first_effect(p_body, initial, bindings, godot_);
			for (Name *name : uses[local]) {
				if (std::none_of(bindings.begin(), bindings.end(), [name](const Binding &p_bind) { return p_bind.access->object == name; })) {
					end = std::min(end, name->offset);
				}
			}
			bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [&, creation = initial](const Binding &p_bind) {
				return last_iteration(parents, creation, p_bind.operation) > end;
			}), bindings.end());
			safe = bindings.size() == uses[local].size();
			if (bindings.empty() && !safe) {
				continue;
			}
		} else if (!safe || (!bindings.empty() && (assigned || (pure_class && initial->kind != NodeKind::New && !(klass->modifiers & Modifiers::Final))))) {
			continue;
		}
		if (safe) {
			local->storage = type;
			for (Name *name : uses[local]) {
				name->type = type;
			}
			for (const auto &[expr, result] : consumers.results) {
				expr->type = result;
			}
		}
		for (const Binding &binding : bindings) {
			binding.access->object->type = type;
			binding.access->member = binding.member;
			binding.access->type = symbol_type(binding.member, nullptr);
			binding.operation->type = binding.result;
		}
	}
}

} // namespace kas3
